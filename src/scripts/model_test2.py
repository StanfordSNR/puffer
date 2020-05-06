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
    permutation = list(np.random.permutation(total_num))
    permutation = permutation[0:sample_num]
    in_arr = list(in_arr[permutation])
    out_arr = list(out_arr[permutation])
    return in_arr, out_arr

def calc_loss(args, day_num, start_dt):
    for i in range(0, day_num):
        date_item = start_dt + timedelta(days=i)
        sample_num_by_day = args.sample_num * ((0.9) **(day_num-i))
        in_data=[]
        out_data = []
        in_arr, out_arr = sample_data(date_item, sample_num_by_day)
        in_data.extend(in_arr)
        out_data.extend(out_arr)
    mdl = Model()
    mdl.load(model_path)
    input_data = model.normalize_input(in_data, update_obs=True)
    output_data = out_data
    training_loss = mdl.compute_loss(input_data, output_data)
    return training_loss

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
    pool = Pool(processes= 5)
    for t in range(14):
        result_procs.append(pool.apply_async(calc_loss, args=(args, day_num, start_dt,)))
    for result in result_procs:
        training_loss = result.get()
        loss_values.append(training_loss)
    print("loss values")
    print(loss_values)
    with open("training_loss-"+args.start_date+"-"+args.end_date+"-"+str(args.sample_num), "w" ) as f:
        f.write(str(loss_values))
        f.close()

    
    








if __name__ == '__main__':
    main()


