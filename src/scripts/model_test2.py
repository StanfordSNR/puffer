#!/usr/bin/env python3 
# python csv_parser.py --file-path ~/Documents/puffer-201903/ --output-path ~/Documents/fork_puffer/training_data_foler --start-date 20190325 --end-date 20190330
import json
import argparse
import yaml
import datetime
import sys
import os
from datetime import datetime, timedelta
import numpy as np
from multiprocessing import Process, Array, Pool
import pandas as pd
import matplotlib
import subprocess
import gc
import torch
from IPython import embed  
from csv_parser_n import(    
    read_csv_to_rows,
    process_raw_csv_data,
prepare_input_output)

from ttp_local3 import Model

# gs://puffer-data-release/2019-01-26T11_2019-01-27T11/video_sent_2019-01-26T11_2019-01-27T11.csv
FUTURE_CHUNKS = 5
model_path ="./model_dir/"

def model_test(pt_file, raw_in_data, raw_out_data, date_str=None):
    model = Model()
    model.load(pt_file)
    input_data = model.normalize_input(raw_in_data, update_obs=False)
    output_data = raw_out_data
    input_data = np.array(input_data)
    output_data = np.array(output_data)
    accuracy, _ = model.compute_accuracy(input_data, output_data)
    del model
    del input_data
    del output_data
    gc.collect()
    return accuracy

def sample_data(date_item, sample_num, model_idx =0 ):
    print("date_item=", date_item, " sample_num=",sample_num)
    next_date_item = date_item+timedelta(days=1)
    date_str1 = str(date_item.year)+"-"+str(date_item.month).zfill(2)+"-"+str(date_item.day).zfill(2)+"T11"
    date_str2 = str(next_date_item.year)+"-"+str(next_date_item.month).zfill(2)+"-"+str(next_date_item.day).zfill(2)+"T11"
    date_str = date_str1+"_"+date_str2
    data_file = date_str+"-"+str(model_idx)+".json"
    if os.path.exists(data_file) is not True:
        print(data_file, " NOT Exists")
        return 1
    print("Loading...", data_file)
    d = json.load(open(data_file))
    print("Loaded...", data_file)
    total_num = len(d['out'])
    in_arr = np.array(d['in'])
    out_arr = np.array(d['out'])
    embed()
    permutation = list(np.random.permutation(total_num))
    permutation = permutation[0:sample_num]
    in_arr = list(in_arr[permutation])
    out_arr = list(out_arr[permutation])
    return in_arr, out_arr

def calc_loss(args, day_num, start_dt):
    in_data=[]
    out_data = []
    weights = [(0.9**(i+1)) for i in range(day_num)]
    total_weights = np.sum(weights)
    sample_nums = [( weights[day_num-i-1]/total_weights *args.sample_num ) for i in range(day_num)]
    day_num = 1
    for i in range(day_num):
        date_item = start_dt + timedelta(days=i)
        sample_num_by_day = int(sample_nums[i])
        sample_num_by_day =100
        print(date_item, " sample_num=",args.sample_num, " ", sample_num_by_day, " ", (0.9 **(day_num-i)))
        in_arr, out_arr = sample_data(date_item, sample_num_by_day)
        in_data.extend(in_arr)
        out_data.extend(out_arr)
 
    num_training = len(in_data)
    model = Model()
    model.load(args.model_file)
    input_data = np.array(in_data)
    output_data = np.array(out_data)

    # # loop over the entire dataset multiple times
    # input_data = model.normalize_input(input_data, update_obs=False)
    # # discretize output data
    # output_data = model.discretize_output(output_data)
    # BATCH_SIZE = 32
    # num_batches = int(np.ceil(num_training / BATCH_SIZE))
    # # permutate data in each epoch
    # perm_indices = np.random.permutation(num_training)
    # running_loss = 0
    # for batch_id in range(num_batches):
    #     start = batch_id * BATCH_SIZE
    #     end = min(start + BATCH_SIZE, num_training)
    #     batch_indices = perm_indices[start:end]
    #     # get a batch of input data
    #     batch_input = input_data[batch_indices]
    #     batch_output = output_data[batch_indices]
    #     x = torch.from_numpy(batch_input)
    #     y = torch.from_numpy(batch_output)
    #     # forward pass
    #     y_scores = model(x)
    #     loss = model.loss_fn(y_scores, y)
    #     running_loss += loss
    # running_loss /= num_batches
    # print("running_loss ", running_loss)
    # return running_loss

    print("date size=", input_data.shape, output_data.shape)
    input_data = model.normalize_input(input_data, update_obs=False)
    output_data = model.discretize_output(output_data)
    print("in shape ", input_data.shape, " out shape ",output_data.shape)
    training_loss = model.compute_loss(input_data, output_data)
    print("training_loss=",training_loss)
    return training_loss
    #mdl.compute_accuracy(input_data, output_data)

def main():
    parser = argparse.ArgumentParser()                     
    parser.add_argument('--start-date', dest='start_date',
                        help='The start date of the model training')  
    parser.add_argument('--end-date', dest='end_date',
                        help='The end_date date of the model training')  
    parser.add_argument('--sample-num', dest='sample_num', default = 1000000, type=int,
                        help='The total number of training samples') 
    parser.add_argument('--model-file', dest='model_file', default = "./models-0/bbr-20200426-py-0.pt",
                        help='The total number of training samples') 
    args = parser.parse_args()    

    start_dt = datetime.strptime(args.start_date,"%Y%m%d")
    end_dt = datetime.strptime(args.end_date,"%Y%m%d")
    day_num = (end_dt - start_dt).days+1

    loss_values = []
    result_procs = []
    calc_loss(args, day_num, start_dt)
    # pool = Pool(processes= 5)
    # for t in range(1):
    #     result_procs.append(pool.apply_async(calc_loss, args=(args, day_num, start_dt,)))
    # for result in result_procs:
    #     training_loss = result.get()
    #     loss_values.append(training_loss)
    # print("loss values")
    # print(loss_values)
    # with open("training_loss-"+args.start_date+"-"+args.end_date+"-"+str(args.sample_num), "w" ) as f:
    #     f.write(str(loss_values))
    #     f.close()

    
    

if __name__ == '__main__':
    main()


