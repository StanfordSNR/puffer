#!/usr/bin/env python3
# Demo Commands: 
# 1) Test a trained model and output the accuracy, mse, etc 
# python ttp_local3.py --load-model model-20200101-20200115-10000/py-0.pt --test-data  /mnt/disks/mnt_dir/training_data/2020-01-17-1
# 2) Train a model from scratch, and save the model as test.pt
# python ttp_local3.py --is-training --static-training  --fresh-data  /mnt/disks/mnt_dir/training_data/2020-01-17-1 --fresh-size 10000 --epoch-num 500 --save-model test.pt
# 3) Train a model based on one existing model (continual training). 
# Load the model from test.pt, continually train the model with the data 2020-01-17-1.in and 2020-01-17-1.out, 
# then after training it for 500 epochs, save it as test-2.pt
# python ttp_local3.py --is-training  --fresh-data  /mnt/disks/mnt_dir/training_data/2020-01-17-1 --fresh-size 10000 --epoch-num 500 --save-model test-2.pt --load-model test.pt

import json
import argparse
import yaml
import torch
import datetime
import sys
from os import path
from datetime import datetime, timedelta
import numpy as np
from multiprocessing import Process, Array, Pool
import pandas as pd
import matplotlib
import gc
matplotlib.use('Agg')
import matplotlib.pyplot as plt

from plot_helpers import (
    distr_bin_pred, distr_l1_pred, distr_l2_pred, l1_loss, l2_loss, bin_acc)

VIDEO_SENT_FILE_PREFIX = 'video_sent_'
VIDEO_ACKED_FILE_PREFIX = 'video_acked_'
CLIENT_BUFFER_FILE_PREFIX = 'client_buffer_'
FILE_SUFFIX = 'T11.csv'
FILE_CHUNK_SIZE = 100000
VIDEO_SENT_KEYS=['timestamp', 'session_id',	
'experiment_id', 'channel_name', 'chunk_presentation_timestamp', 'resolution',
'chunk_size', 'ssim_index',	'cwnd', 'in_flight', 'min_rtt','rtt','delivery_rate']
VIDEO_ACKED_KEYS=['timestamp', 'session_id',	
'experiment_id', 'channel_name', 'chunk_presentation_timestamp']
CLIENT_BUFFER_KEYS=['timestamp', 'session_id',	
'experiment_id', 'channel_name', 'event', 'playback_buffer', 'cumulative_rebuffer']
DEBUG = False


VIDEO_DURATION = 180180
PKT_BYTES = 1500
MILLION = 1000000

# training related
BATCH_SIZE = 32
NUM_EPOCHS = 500
CHECKPOINT = 100

CL_MAX_DATA_SIZE = 1000000  # 1 million rows of data
CL_DISCOUNT = 0.9  # sampling weight discount
CL_MAX_DAYS = 14  # sample from last 14 days

TUNING = False
DEVICE = torch.device('cpu')



class Model:
    PAST_CHUNKS = 8
    FUTURE_CHUNKS = 5
    DIM_IN = 62
    BIN_SIZE = 0.5  # seconds
    BIN_MAX = 20
    DIM_OUT = BIN_MAX + 1
    DIM_H1 = 64
    DIM_H2 = 64
    WEIGHT_DECAY = 1e-4
    LEARNING_RATE = 1e-4

    def __init__(self, model_path=None):
        # define model, loss function, and optimizer
        self.model = torch.nn.Sequential(
            torch.nn.Linear(Model.DIM_IN, Model.DIM_H1),
            torch.nn.ReLU(),
            torch.nn.Linear(Model.DIM_H1, Model.DIM_H2),
            torch.nn.ReLU(),
            torch.nn.Linear(Model.DIM_H2, Model.DIM_OUT),
        ).double().to(device=DEVICE)
        self.loss_fn = torch.nn.CrossEntropyLoss().to(device=DEVICE)
        self.optimizer = torch.optim.Adam(self.model.parameters(),
                                          lr=Model.LEARNING_RATE,
                                          weight_decay=Model.WEIGHT_DECAY)

        self.obs_size = None
        self.obs_mean = None
        self.obs_std = None

    def set_model_train(self):
        self.model.train()

    def set_model_eval(self):
        self.model.eval()

    def update_obs_stats(self, raw_in):
        if self.obs_size is None:
            self.obs_size = len(raw_in)
            self.obs_mean = np.mean(raw_in, axis=0)
            self.obs_std = np.std(raw_in, axis=0)
            return

        # update population size
        old_size = self.obs_size
        new_size = len(raw_in)
        self.obs_size = old_size + new_size

        # update popultation mean
        old_mean = self.obs_mean
        new_mean = np.mean(raw_in, axis=0)
        self.obs_mean = (old_mean * old_size + new_mean * new_size) / self.obs_size

        # update popultation std
        old_std = self.obs_std
        old_sum_square = old_size * (np.square(old_std) + np.square(old_mean))
        new_sum_square = np.sum(np.square(raw_in), axis=0)
        mean_square = (old_sum_square + new_sum_square) / self.obs_size
        self.obs_std = np.sqrt(mean_square - np.square(self.obs_mean))

    def normalize_input(self, raw_in, update_obs=False):
        z = np.array(raw_in)

        # update mean and std of the data seen so far
        if update_obs:
            self.update_obs_stats(z)

        assert(self.obs_size is not None)

        for col in range(len(self.obs_mean)):
            z[:, col] -= self.obs_mean[col]
            if self.obs_std[col] != 0:
                z[:, col] /= self.obs_std[col]

        return z



    # special discretization: [0, 0.5 * BIN_SIZE)
    # [0.5 * BIN_SIZE, 1.5 * BIN_SIZE), [1.5 * BIN_SIZE, 2.5 * BIN_SIZE), ...
    def discretize_output(self, raw_out):
        z = np.array(raw_out)
        z = np.floor((z + 0.5 * Model.BIN_SIZE) / Model.BIN_SIZE).astype(int)
        return np.clip(z, 0, Model.BIN_MAX)

    # perform one step of training (forward + backward + optimize)
    def train_step(self, input_data, output_data):
        x = torch.from_numpy(input_data).to(device=DEVICE)
        y = torch.from_numpy(output_data).to(device=DEVICE)

        # forward pass
        y_scores = self.model(x)
        loss = self.loss_fn(y_scores, y)

        # backpropagation and optimize
        self.optimizer.zero_grad()
        loss.backward()
        self.optimizer.step()

        return loss.item()

    # compute loss
    def compute_loss(self, input_data, output_data):
        with torch.no_grad():
            x = torch.from_numpy(input_data).to(device=DEVICE)
            y = torch.from_numpy(output_data).to(device=DEVICE)

            y_scores = self.model(x)
            # print("y_scores :", y_scores.shape)
            # print(y_scores)
            # print("y:", y.shape)
            # print(y)           
            loss = self.loss_fn(y_scores, y)

            return loss.item()

    # compute mean square error of the classifier based on its expected value of distribution
    # (input_data and output_data have been normalized)
    def compute_mse(self, proc_id, input_data, output_data):
        result = {'bin':[], 'l1':[], 'l2':[] }
        input_data = np.asarray(input_data)
        output_data = np.asarray(output_data)
        with torch.no_grad():
            x = torch.from_numpy(input_data).to(device=DEVICE)
            y = self.model(x)
            abl_distr = torch.nn.functional.softmax(y, dim=1).double().numpy()
        bin_abl_out = distr_bin_pred(abl_distr)
        l1_abl_out = distr_l1_pred(abl_distr)
        l2_abl_out = distr_l2_pred(abl_distr)

        #print("LS VS ", l2_abl_out[:10], " VS ", output_data[:10])
        print(len(input_data)," ", len(output_data) )
        bacc = bin_acc(bin_abl_out, output_data)
        loss1 = l1_loss(l1_abl_out, output_data)
        loss2 = l2_loss(l2_abl_out, output_data)

        print("sum ", np.sum(loss2), " size ", np.size(loss2))
        result['bin'] = np.sum(bacc)/np.size(bacc)
        result['l1']  =  np.sum(loss1)/np.size(loss1)
        result['l2'] = np.sum(loss2)/np.size(loss2)
        #print(result)
        return result

    # compute accuracy of the classifier
    def compute_accuracy(self, input_data, output_data):
        correct = 0
        total = 0

        with torch.no_grad():
            x_tensor = torch.from_numpy(input_data).to(device=DEVICE)
            y_tensor = torch.from_numpy(output_data).to(device=DEVICE)
            total += y_tensor.size(0)
            y_arr = self.discretize_output(output_data)
            y_tensor = torch.from_numpy(y_arr)
            y_scores = self.model(x_tensor)
            y_predicted = torch.max(y_scores, 1)[1].to(device=DEVICE)
            # print("y_tensor shape ", y_tensor.shape)
            # print("y_predicted shape ", y_predicted.shape)
            compare_result = (y_predicted == y_tensor)
            correct += compare_result.sum().item()
            compare_result = [y_predicted.numpy(), y_tensor.numpy()]
        
        return correct / total, compare_result



    def predict(self, input_data):
        with torch.no_grad():
            x = torch.from_numpy(input_data).to(device=DEVICE)

            y_scores = self.model(x)
            y_predicted = torch.max(y_scores, 1)[1].to(device=DEVICE)

            ret = y_predicted.double().numpy()
            for i in range(len(ret)):
                bin_id = ret[i]
                if bin_id == 0:  # the first bin is defined differently
                    ret[i] = 0.25 * Model.BIN_SIZE
                else:
                    ret[i] = bin_id * Model.BIN_SIZE

            return ret

    def load(self, model_path):
        checkpoint = torch.load(model_path)
        self.model.load_state_dict(checkpoint['model_state_dict'])

        self.obs_size = checkpoint['obs_size']
        self.obs_mean = checkpoint['obs_mean']
        self.obs_std = checkpoint['obs_std']

    def save(self, model_path):
        assert(self.obs_size is not None)

        torch.save({
            'model_state_dict': self.model.state_dict(),
            'obs_size': self.obs_size,
            'obs_mean': self.obs_mean,
            'obs_std': self.obs_std,
        }, model_path)

    def save_cpp_model(self, model_path, meta_path):
        # save model to model_path
        example = torch.rand(1, Model.DIM_IN).double()
        traced_script_module = torch.jit.trace(self.model, example)
        traced_script_module.save(model_path)

        # save obs_size, obs_mean, obs_std to meta_path
        meta = {'obs_size': self.obs_size,
                'obs_mean': self.obs_mean.tolist(),
                'obs_std': self.obs_std.tolist()}
        with open(meta_path, 'w') as fh:
            json.dump(meta, fh)

def train(i, args, model, input_data, output_data):
    print("train ", i, input_data.shape, output_data.shape)
    if TUNING:
        # permutate input and output data before splitting
        perm_indices = np.random.permutation(len(input_data))
        input_data = input_data[perm_indices]
        output_data = output_data[perm_indices]

        # split training data into training/validation
        num_training = int(0.8 * len(input_data))
        train_input = input_data[:num_training]
        train_output = output_data[:num_training]
        validate_input = input_data[num_training:]
        validate_output = output_data[num_training:]
        sys.stderr.write('[{}] training set size: {}\n'
                         .format(i, len(train_input)))
        sys.stderr.write('[{}] validation set size: {}\n'
                         .format(i, len(validate_input)))

        validate_losses = []
    else:
        num_training = len(input_data)
        sys.stderr.write('[{}] training set size: {}\n'
                         .format(i, num_training))

    train_losses = []

    # number of batches
    num_batches = int(np.ceil(num_training / BATCH_SIZE))
    sys.stderr.write('[{}] total epochs: {}\n'.format(i, args.epoch_num ))

    # loop over the entire dataset multiple times
    for epoch_id in range(1, 1 + args.epoch_num):
        # permutate data in each epoch
        perm_indices = np.random.permutation(num_training)
        running_loss = 0
        for batch_id in range(num_batches):
            start = batch_id * BATCH_SIZE
            end = min(start + BATCH_SIZE, num_training)
            batch_indices = perm_indices[start:end]

            # get a batch of input data
            batch_input = input_data[batch_indices]
            batch_output = output_data[batch_indices]

            running_loss += model.train_step(batch_input, batch_output)
        running_loss /= num_batches

        # print info
        if TUNING:
            train_loss = model.compute_loss(train_input, train_output)
            validate_loss = model.compute_loss(validate_input, validate_output)
            train_losses.append(train_loss)
            validate_losses.append(validate_loss)

            train_accuracy = 100 * model.compute_accuracy(
                    train_input, train_output)
            validate_accuracy = 100 * model.compute_accuracy(
                    validate_input, validate_output)

            sys.stderr.write('[{}] epoch {}:\n'
                             '\ttraining: loss {:.3f}, accuracy {:.2f}%\n'
                             '\tvalidation: loss {:.3f}, accuracy {:.2f}%\n'
                             .format(i, epoch_id,
                                     train_loss, train_accuracy,
                                     validate_loss, validate_accuracy))
        else:
            train_losses.append(running_loss)
            sys.stderr.write('[{}] epoch {}: training loss {:.3f}\n'
                             .format(i, epoch_id, running_loss))

        # save checkpoints or the final model
        if epoch_id % CHECKPOINT == 0 or epoch_id == args.epoch_num:
            if epoch_id == args.epoch_num:
                suffix = ''
            else:
                suffix = '-checkpoint-{}'.format(epoch_id)

            model_path = args.save_model+suffix
            model.save(model_path)
            sys.stderr.write('[{}] Saved model for Python to {}\n'
                             .format(i, model_path))


def train_or_eval_model(i, args, raw_in_data, raw_out_data, model):
    # reduce number of threads as we're running FUTURE_CHUNKS parallel processes
    num_threads = max(1, int(torch.get_num_threads() / Model.FUTURE_CHUNKS))
    torch.set_num_threads(num_threads)
    # # create or load a model
    # model = Model()
    # if args.static_training is False:
    #     #model_path = path.join(args.load_model, 'py-{}.pt'.format(i))
    #     model.load(args.load_model)
    input_data = model.normalize_input(raw_in_data, update_obs=True)
    # discretize output data
    output_data = model.discretize_output(raw_out_data)
    model.set_model_train()
    # train a neural network with data
    train(i, args, model, input_data, output_data)

def test_model(proc_id, args, raw_in_data, raw_out_data, model):
    # normalize input data
    # model = Model()
    # model.set_model_eval()
    # if args.load_model:
    #     model_path = args.load_model
    #     model.load(model_path)
    #     sys.stderr.write('[{}] Loaded model from {}\n'.format(proc_id, model_path))
    # else:
    #     sys.stderr.write('No model path specified\n')
    write_line = ""
    input_data = model.normalize_input(raw_in_data, update_obs=False)
    output_data = raw_out_data
    print("out_data ", len(output_data))
    write_line += "out_data " + str(len(output_data))+"\n"
    mse = model.compute_mse(proc_id, input_data, output_data)
    print("result= ", mse)
    write_line += "result= " + str(mse)+"\n"
    input_data = np.array(input_data)
    output_data = np.array(output_data)
    accuracy, result_arr = model.compute_accuracy(input_data, output_data)
    print("result= ", accuracy)
    predicted_values = result_arr[0].tolist()
    original_values = result_arr[1].tolist()

    with open("comp", "w") as cmpf:
        for i in range(len(predicted_values)):
            cmpf.write(str(predicted_values[i])+"\t"+str(original_values[i])+"\n")
    write_line += "result= "+ str(accuracy)+"\n"
    if args.save_model is not None:
        result_file = open(args.save_model+"-result", "w" )
        result_file.write(write_line)
    if args.add_suffix is not None:
        args.test_data += args.add_suffix
    if args.classify is True:
        # We classify the test data into two parts. 
        # Positive ones are correctly predicted by the model
        # Negative ones are not correctly predicted by the model
        pos_in = open(args.test_data+"-pos.in", "w")
        pos_out = open(args.test_data+"-pos.out", "w")
        neg_in = open(args.test_data+"-neg.in", "w")
        neg_out = open(args.test_data+"-neg.out", "w")
        for i in range(len(result_arr)):
            if result_arr[i] == 1:
                pos_in.write(str(raw_in_data[i])+"\n")
                pos_out.write(str([raw_out_data[i]])+"\n")
            else:
                neg_in.write(str(raw_in_data[i])+"\n")
                neg_out.write(str([raw_out_data[i]])+"\n")
        pos_in.close()
        pos_out.close()
        neg_in.close()
        neg_out.close()


def read_data(file_name, sample_size, lazy_read=None):
    ret_in = []
    ret_out = []
    in_file_name = file_name+".in"
    cnt = 0
    with open(in_file_name) as f:
        for line in f:
            arr = eval(line)
            ret_in.append(arr)
            cnt += 1
            if cnt % 10000 == 0:
                print("cnt = ", cnt)
            if lazy_read is True and cnt >= sample_size:
                break;
            
    f.close()
    out_file_name = file_name+".out"
    with open(out_file_name) as f:
        for line in f:
            arr = eval(line)
            ret_out.extend(arr)
    ret_out=ret_out[:len(ret_in)]
    f.close()
    print("read finished")
    if sample_size is None:
        return ret_in, ret_out
    perm_indices = np.random.permutation(len(ret_in))[:sample_size]
    in_data = []
    out_data = []
    for i in range(sample_size):
        idx = perm_indices[i]
        in_data.append(ret_in[idx])
        out_data.append(ret_out[idx])
    return in_data, out_data


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--is-training', dest='is_training', action='store_true', 
                        help='is training or testing')
    parser.add_argument('--static-training', dest='static_training', action='store_true', 
                        help='static training (or continual learning)')
    parser.add_argument('--compute-mse', dest='compute_mse', action='store_true', 
                        help='whether compute the mean square error')
    parser.add_argument('--train-and-test', dest='train_and_test', action='store_true', 
                        help='train and test')
    parser.add_argument('--classify', dest='classify', action='store_true', 
                        help='output positive and negative samples')
    # While training the model, the training data may come from two sources
    # For example, suppose we are training a model on 20200120, we may use the data 
    # sampled from 20200120, besides, we may also use the data collected before 20200120, 
    # so we specify two sources to study the impact on performance with different portions 
    # of fresh/old data
    parser.add_argument('--fresh-data', dest='fresh_data',
                        help='file path of the training data')    
    parser.add_argument('--fresh-size', dest='fresh_size', type=int,
                        help='sample size of fresh data') 
    parser.add_argument('--old-data', dest='old_data', 
                        help='file path of old data')     
    parser.add_argument('--old-size', dest='old_size', type=int,
                        help='sample size of old data')  
    parser.add_argument('--epoch-number', dest='epoch_num', type=int,
                        help='number of training epochs (i.e. how many times does each training sample have been put into the model)')                   
    parser.add_argument('--test-data', dest='test_data',
                        help='file path of test-data')  
    parser.add_argument('--additional-suffix', dest='add_suffix',
                        help='additional-suffix')    
    parser.add_argument('--test-size', dest='test_size', type=int,
                        help='sample size of test data')  
    parser.add_argument('--load-model', dest='load_model',
        help='the path of the model to load (while in testing mode)')
    parser.add_argument('--save-model',dest='save_model',
        help='the path to save the model (after we have finished training)')
    
    args = parser.parse_args()

    if args.train_and_test:
        ret_in, ret_out = read_data(args.fresh_data, args.fresh_size)
        if args.old_data is not None:
            old_in, old_out = read_data(args.old_data, args.old_size, True)
            ret_in.extend(old_in)
            ret_out.extend(old_out)
        # shuffle the training data
        perm_indices = np.random.permutation(len(ret_in))
        ret_in = np.array(ret_in)
        ret_out = np.array(ret_out)
        ret_in = ret_in[perm_indices]
        ret_out = ret_out[perm_indices]
        # For the train/test, it can be parallelized with multiprocessing 
        # (The first parameter in train_or_eval_model/test_model is the proc-no). 
        # However, for convenience, I only use single process, and train/test one model at a time.
        model = Model()
        if args.static_training is False:
            #model_path = path.join(args.load_model, 'py-{}.pt'.format(i))
            model.load(args.load_model)
        train_or_eval_model(0, args, ret_in, ret_out, model)
        ret_in, ret_out = read_data(args.test_data, args.test_size)
        model = Model()
        model.load(args.save_model)
        test_model(0, args, ret_in, ret_out, model)      
    elif args.is_training:
        # train
        ret_in, ret_out = read_data(args.fresh_data, args.fresh_size)
        if args.old_data is not None:
            old_in, old_out = read_data(args.old_data, args.old_size)
            ret_in.extend(old_in)
            ret_out.extend(old_out)
        # shuffle the training data
        perm_indices = np.random.permutation(len(ret_in))
        ret_in = np.array(ret_in)
        ret_out = np.array(ret_out)
        ret_in = ret_in[perm_indices]
        ret_out = ret_out[perm_indices]
        # For the train/test, it can be parallelized with multiprocessing 
        # (The first parameter in train_or_eval_model/test_model is the proc-no). 
        # However, for convenience, I only use single process, and train/test one model at a time.
            # create or load a model
        model = Model()
        if args.static_training is False:
            #model_path = path.join(args.load_model, 'py-{}.pt'.format(i))
            model.load(args.load_model)
        train_or_eval_model(0, args, ret_in, ret_out, model)
    else:
        # test
        ret_in, ret_out = read_data(args.test_data, args.test_size)
            # normalize input data
        model = Model()
        model.set_model_eval()
        if args.load_model:
            model_path = args.load_model
            model.load(model_path)
            sys.stderr.write('[{}] Loaded model from {}\n'.format(0, model_path))
        else:
            sys.stderr.write('No model path specified\n')
        test_model(0, args, ret_in, ret_out, model)



if __name__ == '__main__':
    main()
