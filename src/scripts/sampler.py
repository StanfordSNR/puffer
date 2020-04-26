#!/usr/bin/env python3

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

repo = "/mnt/disks/mnt_dir/training_data/"
def read_data(file_name, number):
    in_file = file_name+".in"
    out_file = file_name +".out"
    ret_in = []
    ret_out = []
    cnt = 0
    with open(in_file, "r") as in_f:
        for line in in_f:
            ret_in.append(line)
            cnt += 1
            if cnt % 10000 == 0:
                print("cnt =", cnt)
    with open(out_file, "r") as out_f:
        for line in out_f:
            arr =eval(line)
            ret_out.extend(arr)
    perm_indices = np.random.permutation(len(ret_in))[:number]
    ret_in = np.array(ret_in)
    ret_out = np.array(ret_out)
    ret_in = ret_in[perm_indices]
    ret_out = ret_out[perm_indices]
    return ret_in, ret_out

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--repo-name', dest='repo', default="/mnt/disks/mnt_dir/training_data/",
                        help='the path of repo')  
    parser.add_argument('--start-date', dest='start_date',
                        help='start date of the training data (e.g.20190301)')  
    parser.add_argument('--end-date', dest='end_date',
                        help='end date of the training data')    
    parser.add_argument('--number', dest='number', type = int,
                        help='sampled number ')
    parser.add_argument('--save-path', dest='save_path',
                        help='the file path to save the data ')
    
    args = parser.parse_args()
    start_dt = datetime.strptime(args.start_date,"%Y%m%d")
    end_dt = datetime.strptime(args.end_date,"%Y%m%d")
    day_num = (end_dt - start_dt).days+1
    in_data = []
    out_data = []
    for i in range(day_num):
        dt =start_dt + timedelta(days=i)
        fname = str(dt.year)+"-"+str(dt.month).zfill(2)+"-"+str(dt.day).zfill(2)+"-1"
        file_name = "{repo}{fname}".format(repo=args.repo, fname= fname)
        ret_in, ret_out =read_data(file_name, args.number//day_num+100)
        in_data.extend(ret_in)
        out_data.extend(ret_out)
    in_data = np.array(in_data)
    out_data = np.array(out_data)
    perm_indices = np.random.permutation(len(in_data))
    in_data = in_data[perm_indices]
    out_data = out_data[perm_indices]
    in_data = in_data[:args.number]
    out_data = out_data[:args.number]
    sampled_in =  open(args.save_path+".in", "w")
    sampled_out = open(args.save_path+".out", "w")
    for i in range(len(in_data)):
        sampled_in.write(in_data[i])
        sampled_out.write(str([out_ddata[i]])+"\n")
    sampled_in.close()
    sampled_out.close()

if __name__ == '__main__':
    main()