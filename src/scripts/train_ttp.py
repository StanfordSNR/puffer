#!/usr/bin/env python3

import sys
import argparse
import yaml
import torch
from os import path
import numpy as np
from multiprocessing import Process

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

from helpers import connect_to_influxdb, try_parsing_time, make_sure_path_exists


VIDEO_DURATION = 180180
PKT_BYTES = 1500
MILLION = 1000000

# training related
BATCH_SIZE = 64

TUNING = False
DEVICE = torch.device('cpu')


class Model:
    PAST_CHUNKS = 8
    FUTURE_CHUNKS = 5
    DIM_IN = 62
    BIN_SIZE = 0.5  # seconds
    BIN_MAX = 20
    DIM_OUT = BIN_MAX + 1
    DIM_H1 = 50
    DIM_H2 = 50
    WEIGHT_DECAY = 1e-3
    LEARNING_RATE = 1e-3
    SMALLER_LEARNING_RATE = 1e-4

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

        # load model
        if model_path:
            self.first_training = False

            checkpoint = torch.load(model_path)
            self.model.load_state_dict(checkpoint['model_state_dict'])

            self.obs_size = checkpoint['obs_size']
            self.obs_mean = checkpoint['obs_mean']
            self.obs_std = checkpoint['obs_std']

            self.optimizer = torch.optim.SGD(self.model.parameters(),
                                             lr=Model.SMALLER_LEARNING_RATE,
                                             weight_decay=Model.WEIGHT_DECAY)
        else:
            self.first_training = True

            self.obs_size = None
            self.obs_mean = None
            self.obs_std = None

            self.optimizer = torch.optim.Adam(self.model.parameters(),
                                              lr=Model.LEARNING_RATE,
                                              weight_decay=Model.WEIGHT_DECAY)

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

    def normalize_input(self, raw_in):
        z = np.array(raw_in)

        # update mean and std of the data seen so far
        self.update_obs_stats(z)

        for col in range(len(self.obs_mean)):
            z[:, col] -= self.obs_mean[col]
            if self.obs_std[col] != 0:
                z[:, col] /= self.obs_std[col]

        return z

    def discretize_output(self, raw_out):
        z = np.array(raw_out)

        z = np.floor(z / Model.BIN_SIZE).astype(int)
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
            loss = self.loss_fn(y_scores, y)

            return loss.item()

    # compute accuracy of the classifier
    def compute_accuracy(self, input_data, output_data):
        correct = 0
        total = 0

        with torch.no_grad():
            x = torch.from_numpy(input_data).to(device=DEVICE)
            y = torch.from_numpy(output_data).to(device=DEVICE)

            y_scores = self.model(x)
            y_predicted = torch.max(y_scores, 1)[1].to(device=DEVICE)

            total += y.size(0)
            correct += (y_predicted == y).sum().item()

        return correct / total

    def save(self, model_path):
        torch.save({
            'model_state_dict': self.model.state_dict(),
            'obs_size': self.obs_size,
            'obs_mean': self.obs_mean,
            'obs_std': self.obs_std,
        }, model_path)


def check_args(args):
    if args.load_model:
        if not path.isdir(args.load_model):
            sys.exit('Error: directory {} does not exist'
                     .format(args.load_model))

        for i in range(Model.FUTURE_CHUNKS):
            model_path = path.join(args.load_model, '{}.pt'.format(i))
            if not path.isfile(model_path):
                sys.exit('Error: model {} does not exist'.format(model_path))

    if args.save_model:
        make_sure_path_exists(args.save_model)

        for i in range(Model.FUTURE_CHUNKS):
            model_path = path.join(args.save_model, '{}.pt'.format(i))
            if path.isfile(model_path):
                sys.exit('Error: model {} already exists'.format(model_path))

    if args.inference:
        if not args.load_model:
            sys.exit('Error: need to load model before inference')

        if args.tune or args.save_model:
            sys.exit('Error: cannot tune or save model during inference')
    else:
        if not args.save_model:
            sys.stderr.write('Warning: model will not be saved\n')

    # want to tune hyperparameters
    if args.tune:
        if args.save_model:
            sys.stderr.write('Warning: model would better be trained with '
                             'validation dataset\n')

        global TUNING
        TUNING = True

    # set device to CPU or GPU
    if args.enable_gpu:
        if not torch.cuda.is_available():
            sys.exit('Error: --enable-gpu is set but no CUDA is available')

        global DEVICE
        DEVICE = torch.device('cuda')
        torch.backends.cudnn.benchmark = True


def create_time_clause(time_start, time_end):
    time_clause = None

    if time_start is not None:
        time_clause = "time >= '{}'".format(time_start)
    if time_end is not None:
        if time_clause is None:
            time_clause = "time <= '{}'".format(time_end)
        else:
            time_clause += " AND time <= '{}'".format(time_end)

    return time_clause


def get_ssim_index(pt):
    if 'ssim_index' in pt and pt['ssim_index'] is not None:
        return float(pt['ssim_index'])

    if 'ssim' in pt and pt['ssim'] is not None:
        return ssim_db_to_index(float(pt['ssim']))

    return None


def calculate_trans_times(video_sent_results, video_acked_results):
    d = {}
    last_video_ts = {}

    for pt in video_sent_results['video_sent']:
        session = (pt['user'], int(pt['init_id']),
                   pt['channel'], int(pt['expt_id']))
        if session not in d:
            d[session] = {}
            last_video_ts[session] = None

        video_ts = int(pt['video_ts'])

        if last_video_ts[session] is not None:
            if video_ts != last_video_ts[session] + VIDEO_DURATION:
                sys.exit('Error in session {}: last_video_ts={}, video_ts={}'
                         .format(session, last_video_ts[session], video_ts))
        last_video_ts[session] = video_ts

        d[session][video_ts] = {}
        dsv = d[session][video_ts]  # short name

        dsv['sent_ts'] = try_parsing_time(pt['time'])
        dsv['size'] = float(pt['size']) / PKT_BYTES  # bytes -> packets
        # byte/second -> packet/second
        dsv['delivery_rate'] = float(pt['delivery_rate']) / PKT_BYTES
        dsv['cwnd'] = float(pt['cwnd'])
        dsv['in_flight'] = float(pt['in_flight'])
        dsv['min_rtt'] = float(pt['min_rtt']) / MILLION  # us -> s
        dsv['rtt'] = float(pt['rtt']) / MILLION  # us -> s
        # dsv['ssim_index'] = get_ssim_index(pt)

    for pt in video_acked_results['video_acked']:
        session = (pt['user'], int(pt['init_id']),
                   pt['channel'], int(pt['expt_id']))
        if session not in d:
            sys.stderr.write('Warning: ignored session {}\n'.format(session))
            continue

        video_ts = int(pt['video_ts'])
        if video_ts not in d[session]:
            sys.stderr.write('Warning: ignored acked video_ts {} in the '
                             'session {}\n'.format(video_ts, session))
            continue

        dsv = d[session][video_ts]  # short name

        # calculate transmission time
        sent_ts = dsv['sent_ts']
        acked_ts = try_parsing_time(pt['time'])
        dsv['acked_ts'] = acked_ts
        dsv['trans_time'] = (acked_ts - sent_ts).total_seconds()

    return d


def prepare_raw_data(yaml_settings_path, time_start, time_end):
    with open(yaml_settings_path, 'r') as fh:
        yaml_settings = yaml.safe_load(fh)

    # construct time clause after 'WHERE'
    time_clause = create_time_clause(time_start, time_end)

    # create a client connected to InfluxDB
    influx_client = connect_to_influxdb(yaml_settings)

    # perform queries in InfluxDB
    video_sent_query = 'SELECT * FROM video_sent'
    if time_clause is not None:
        video_sent_query += ' WHERE ' + time_clause
    video_sent_results = influx_client.query(video_sent_query)
    if not video_sent_results:
        sys.exit('Error: no results returned from query: ' + video_sent_query)

    video_acked_query = 'SELECT * FROM video_acked'
    if time_clause is not None:
        video_acked_query += ' WHERE ' + time_clause
    video_acked_results = influx_client.query(video_acked_query)
    if not video_acked_results:
        sys.exit('Error: no results returned from query: ' + video_acked_query)

    # calculate chunk transmission times
    return calculate_trans_times(video_sent_results, video_acked_results)


def append_past_chunks(ds, next_ts, row):
    i = 1
    past_chunks = []

    while i <= Model.PAST_CHUNKS:
        ts = next_ts - i * VIDEO_DURATION
        if ts in ds and 'trans_time' in ds[ts]:
            past_chunks = [ds[ts]['delivery_rate'],
                           ds[ts]['cwnd'], ds[ts]['in_flight'],
                           ds[ts]['min_rtt'], ds[ts]['rtt'],
                           ds[ts]['size'], ds[ts]['trans_time']] + past_chunks
        else:
            nts = ts + VIDEO_DURATION  # padding with the nearest ts
            padding = [ds[nts]['delivery_rate'],
                       ds[nts]['cwnd'], ds[nts]['in_flight'],
                       ds[nts]['min_rtt'], ds[nts]['rtt']]

            if nts == next_ts:
                padding += [0, 0]  # next_ts is the first chunk to send
            else:
                padding += [ds[nts]['size'], ds[nts]['trans_time']]

            break

        i += 1

    if i != Model.PAST_CHUNKS + 1:  # break in the middle; padding must exist
        while i <= Model.PAST_CHUNKS:
            past_chunks = padding + past_chunks
            i += 1

    row += past_chunks


# return FUTURE_CHUNKS pairs of (raw_in, raw_out)
def prepare_input_output(d):
    ret = [{'in':[], 'out':[]} for _ in range(Model.FUTURE_CHUNKS)]

    for session in d:
        ds = d[session]

        for next_ts in ds:
            if 'trans_time' not in ds[next_ts]:
                continue

            # construct a single row of input data
            row = []

            # append past chunks with padding
            append_past_chunks(ds, next_ts, row)

            # append the TCP info of the next chunk
            row += [ds[next_ts]['delivery_rate'],
                    ds[next_ts]['cwnd'], ds[next_ts]['in_flight'],
                    ds[next_ts]['min_rtt'], ds[next_ts]['rtt']]

            # generate FUTURE_CHUNKS rows
            for i in range(Model.FUTURE_CHUNKS):
                row_i = row.copy()

                ts = next_ts + i * VIDEO_DURATION
                if ts in ds and 'trans_time' in ds[ts]:
                    row_i += [ds[ts]['size']]

                    assert(len(row_i) == Model.DIM_IN)
                    ret[i]['in'].append(row_i)
                    ret[i]['out'].append(ds[ts]['trans_time'])

    return ret


def print_stats(i, output_data):
    # print label distribution
    bin_sizes = np.zeros(Model.BIN_MAX + 1, dtype=int)
    for bin_id in output_data:
        bin_sizes[bin_id] += 1
    sys.stderr.write('[{}] label distribution:\n\t'.format(i))
    for bin_size in bin_sizes:
        sys.stderr.write(' {}'.format(bin_size))
    sys.stderr.write('\n')

    # predict a single label
    sys.stderr.write('[{}] single label accuracy: {:.2f}%\n'
                     .format(i, 100 * np.max(bin_sizes) / len(output_data)))


def plot_loss(losses, figure_path):
    fig, ax = plt.subplots()

    if 'training' in losses:
        ax.plot(losses['training'], 'g--', label='training')
    if 'validation' in losses:
        ax.plot(losses['validation'], 'r-', label='validation')

    ax.set_xlabel('Epoch')
    ax.set_ylabel('Loss')
    ax.grid()
    ax.legend()

    fig.savefig(figure_path, dpi=300, bbox_inches='tight', pad_inches=0.2)
    sys.stderr.write('Saved plot to {}\n'.format(figure_path))


def train(i, model, input_data, output_data):
    if TUNING:
        # permutate input and output data before splitting
        perm_indices = np.random.permutation(range(len(input_data)))
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

    num_epochs = 500 if model.first_training else 10
    sys.stderr.write('[{}] total epochs: {}\n'.format(i, num_epochs))

    # loop over the entire dataset multiple times
    for epoch_id in range(num_epochs):
        # permutate data in each epoch
        perm_indices = np.random.permutation(range(num_training))

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
                             .format(i, epoch_id + 1,
                                     train_loss, train_accuracy,
                                     validate_loss, validate_accuracy))
        else:
            train_losses.append(running_loss)
            sys.stderr.write('[{}} epoch {}: training loss {:.3f}\n'
                             .format(i, epoch_id + 1, running_loss))

    # return losses for plotting
    losses = {}
    losses['training'] = train_losses
    if TUNING:
        losses['validation'] = validate_losses
    return losses


def train_or_eval_model(i, args, raw_in_data, raw_out_data):
    if args.load_model:
        model_path = path.join(args.load_model, '{}.pt'.format(i))
        model = Model(model_path)
        sys.stderr.write('[{}] Loaded model from {}\n'.format(i, model_path))
    else:
        model = Model()
        sys.stderr.write('[{}] Created a new model\n'.format(i))

    # normalize input data
    # save normalization weights on first training
    input_data = model.normalize_input(raw_in_data)

    # discretize output data
    output_data = model.discretize_output(raw_out_data)

    # print some stats
    print_stats(i, output_data)

    if args.inference:
        model.set_model_eval()

        sys.stderr.write('[{}] test set size: {}\n'.format(i, len(input_data)))
        sys.stderr.write('[{}] loss: {:.3f}, accuracy: {:.2f}%\n'
            .format(i, model.compute_loss(input_data, output_data),
                    100 * model.compute_accuracy(input_data, output_data)))
    else:  # training
        model.set_model_train()

        # train a neural network with data
        losses = train(i, model, input_data, output_data)

        if args.save_model:
            model_path = path.join(args.save_model, '{}.pt'.format(i))
            model.save(model_path)
            sys.stderr.write('[{}] Saved model to {}\n'.format(i, model_path))

        plot_loss(losses, 'loss{}.png'.format(i))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('yaml_settings')
    parser.add_argument('--from', dest='time_start',
                        help='datetime in UTC conforming to RFC3339')
    parser.add_argument('--to', dest='time_end',
                        help='datetime in UTC conforming to RFC3339')
    parser.add_argument('--load-model',
        help='folder to load {:d} models from'.format(Model.FUTURE_CHUNKS))
    parser.add_argument('--save-model',
        help='folder to save {:d} models to'.format(Model.FUTURE_CHUNKS))
    parser.add_argument('--enable-gpu', action='store_true')
    parser.add_argument('--tune', action='store_true')
    parser.add_argument('--inference', action='store_true')
    args = parser.parse_args()

    # validate and process args
    check_args(args)

    # query InfluxDB and retrieve raw data
    raw_data = prepare_raw_data(args.yaml_settings,
                                args.time_start, args.time_end)

    # collect input and output data from raw data
    raw_in_out = prepare_input_output(raw_data)

    # train or test FUTURE_CHUNKS models
    proc_list = []
    for i in range(Model.FUTURE_CHUNKS):
        proc = Process(target=train_or_eval_model,
                       args=(i, args,
                             raw_in_out[i]['in'], raw_in_out[i]['out'],))
        proc.start()
        proc_list.append(proc)

    # wait for all processes to finish
    for proc in proc_list:
        proc.join()


if __name__ == '__main__':
    main()
