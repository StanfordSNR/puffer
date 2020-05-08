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

result_folder="./ans2/"
def main(args):
    start_dt = datetime.strptime(args.start_date,"%Y%m%d")
    end_dt = datetime.strptime(args.end_date,"%Y%m%d")
    day_num = (end_dt - start_dt).days+1 
    for i in range(day_num):
        date_item = start_dt + timedelta(days=i)
        next_date_item = start_dt + timedelta(days=i+1)
        merged_file_name = (str(date_item.year)+"-"+str(date_item.month).zfill(2)+"-"+str(date_item.day).zfill(2)+"T11"
                            +"_"+str(next_date_item.year)+"-"+str(next_date_item.month).zfill(2)+"-"+str(next_date_item.day).zfill(2)+"T11"
                            +"-result")
        if os.path.exists(result_folder+merged_file_name):
            continue
        else:
            partition_file_exist = True
            result_dict = {}
            for j in range(5):
                partition_file_name = merged_file_name+"-"+str(j)
                if (os.path.exists(result_folder+partition_file_name) is False 
                    or os.path.getsize(result_folder+partition_file_name) ==0 ):
                    partition_file_exist= False
                    break 
                else:
                    print("Loading.. ", partition_file_name)
                    r = json.load(open(result_folder+partition_file_name) )
                    result_dict.update(r)
            if partition_file_exist:
                with open(result_folder+merged_file_name, "w") as f:
                    f.write(json.dumps(result_dict))
                    f.close()
            else:
                print("No Exists ", date_item)

def check(args):
    start_dt = datetime.strptime(args.start_date,"%Y%m%d")
    end_dt = datetime.strptime(args.end_date,"%Y%m%d")
    day_num = (end_dt - start_dt).days+1 
    for i in range(day_num):
        date_item = start_dt + timedelta(days=i)
        next_date_item = start_dt + timedelta(days=i+1)
        merged_file_name = (str(date_item.year)+"-"+str(date_item.month).zfill(2)+"-"+str(date_item.day).zfill(2)+"T11"
                            +"_"+str(next_date_item.year)+"-"+str(next_date_item.month).zfill(2)+"-"+str(next_date_item.day).zfill(2)+"T11"
                            +"-result")
        if os.path.exists(result_folder+merged_file_name):
            result_dict = json.load(open(result_folder+merged_file_name))
            if len(result_dict)<2229:
                print("Problem ", date_item, "len ", len(result_dict))
        else:
            print("No Exist ", date_item)        


if __name__ == '__main__':
    parser = argparse.ArgumentParser()                     
    parser.add_argument('--start-date', dest='start_date',
                        help='The start date of the model training')  
    parser.add_argument('--end-date', dest='end_date',
                        help='The end_date date of the model training')
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()   
    if args.check:
        check(args)
    else:
        main(args)


