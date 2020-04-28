#!/usr/bin/env python3 
# python csv_parser.py --file-path ~/Documents/puffer-201903/ --output-path ~/Documents/fork_puffer/training_data_foler --start-date 20190325 --end-date 20190330
import json
import argparse
import yaml
import torch
import datetime
import sys
from os
from datetime import datetime, timedelta
import numpy as np
from multiprocessing import Process, Array, Pool
import pandas as pd
import matplotlib
import subprocess
import gc

FUTURE_CHUNKS = 5
#gsutil cp -r  gs://puffer-models/puffer-ttp/bbr-20190124-1.tar.gz
def extract_model_file(cc, date_item, dir_name):

    tar_file_name =cc+"-"+date_item.strftime('%Y%m%d')+"-1.tar.gz"
    path_name = "gs://puffer-models/puffer-ttp/"
    cmd = "gsutil cp "+path_name+tar_file_name+" ./"
    subprocess.call(cmd, shell=True)
    cmd = "tar -zxvf "+tar_file_name
    subprocess.call(cmd, shell=True)
    for i in range(FUTURE_CHUNKS):
        model_name = "py-"+str(i)+".pt"
        new_model_name = date_item.strftime('%Y%m%d')+"-"+model_name
        cmd = "cp "+model_name+" "+dir_name+"/"+new_model_name
        subprocess.call(cmd, shell=True)
    print("FIN: "+tar_file_name)


def main():
    parser = argparse.ArgumentParser()                     
    parser.add_argument('--congestion-control', dest='cc',
                        help='Congestion control used while training this model')  
    parser.add_argument('--start-date', dest='start_date',
                        help='The start date of the model training')  
    parser.add_argument('--end-date', dest='end_date',
                        help='The end_date date of the model training')  

    args = parser.parse_args()

    start_dt = datetime.strptime(args.start_date,"%Y%m%d")
    end_dt = datetime.strptime(args.end_date,"%Y%m%d")
    day_num = (end_dt - start_dt).days+1
    dir_name = args.cc+"_models"
    if os.path.exists(dir_name) is False:
        os.mkdir(dir_name)

    for i in range(day_num):
        date_item = start_dt + timedelta(days=i)
        extract_model_file(args.cc,date_item, dir_name)
        


if __name__ == '__main__':
    main()