#!/usr/bin/env python3 
# python csv_parser.py --file-path ~/Documents/puffer-201903/ --output-path ~/Documents/fork_puffer/training_data_foler --start-date 20190325 --end-date 20190330
import json
import argparse
import yaml
import datetime
import sys
import time
import os
from datetime import datetime, timedelta
import numpy as np
from multiprocessing import Process, Array, Pool
import pandas as pd
import matplotlib
import subprocess
import gc
import subprocess
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

def parse_file(video_sent_file, video_acked_file):
    video_sent_rows =  read_csv_to_rows(None, video_sent_file)
    video_acked_rows = read_csv_to_rows(None, video_acked_file)
    print("read_and_write_csv_proc  ", len(video_sent_rows), " ", 
        len(video_acked_rows),  " complete reading rows, will process the raw csv ")
    raw_data = process_raw_csv_data(video_sent_rows, video_acked_rows, None)
    raw_in_out = prepare_input_output(raw_data)
    del video_sent_rows
    del video_acked_file
    gc.collect()
    return raw_in_out
def convert_data(date_item):
    next_date_item = date_item+timedelta(days=1)
    # piece the file name
    date_str1 = str(date_item.year)+"-"+str(date_item.month).zfill(2)+"-"+str(date_item.day).zfill(2)+"T11"
    date_str2 = str(next_date_item.year)+"-"+str(next_date_item.month).zfill(2)+"-"+str(next_date_item.day).zfill(2)+"T11"
    date_str = date_str1+"_"+date_str2
    video_sent_file = "video_sent_"+date_str+".csv"
    video_acked_file = "video_acked_"+date_str+".csv"
    gs_video_sent_file_path = "gs://puffer-data-release/"+date_str+"/"+video_sent_file
    gs_video_acked_file_path =  "gs://puffer-data-release/"+date_str+"/"+video_acked_file
    cmd = "gsutil cp "+gs_video_sent_file_path+" ./"
    subprocess.call(cmd, shell=True)
    cmd = "gsutil cp "+gs_video_acked_file_path+" ./"
    subprocess.call(cmd, shell=True)
    raw_in_out = parse_file(video_sent_file, video_acked_file)
    if len(raw_in_out[0]['in'])== 0 or  len(raw_in_out[0]['out'])== 0:
        cmd = "rm -f "+ video_sent_file+" "+video_acked_file
        subprocess.call(cmd, shell=True)
        return 1

    for j in range(FUTURE_CHUNKS):
        file_name = date_str+"-"+str(j)+".json"
        training_data ={"in":raw_in_out[j]['in'], "out":raw_in_out[j]['out']}
        with open(file_name, "w") as f:
            f.write(json.dumps(training_data))
        f.close()
        print(file_name," Generated")
        del training_data 
        gc.collect()
    del raw_in_out
    gc.collect()
    cmd = "rm -f "+ video_sent_file+" "+video_acked_file
    subprocess.call(cmd, shell=True)
    return 1
def model_test_on_one_portion(date_item, model_idx):
    next_date_item = date_item+timedelta(days=1)
    date_str1 = str(date_item.year)+"-"+str(date_item.month).zfill(2)+"-"+str(date_item.day).zfill(2)+"T11"
    date_str2 = str(next_date_item.year)+"-"+str(next_date_item.month).zfill(2)+"-"+str(next_date_item.day).zfill(2)+"T11"
    date_str = date_str1+"_"+date_str2
    data_file = date_str+"-"+str(model_idx)+".json"
    if os.path.exists(data_file) is not True:
        print(data_file, " NOT Exists")
        return 1
    # Load Model
    model_folder = './models-'+str(model_idx)
    dir1 = os.listdir(model_folder)
    models = []
    for file_name in dir1:
        models.append(file_name)
    print("Loading...", data_file)
    d = json.load(open(data_file))
    print("Loaded...", data_file)
    print(data_file, " ", len(d['in']), " ", len(d['out']) )
    results = {}
    cnt = 0
    for model_file in models:
        pt_file = model_folder+"/"+model_file
        accuracy= model_test(pt_file, d['in'], d['out'], date_str=data_file)
        results[model_file]=accuracy
        cnt += 1
        if cnt% 10 == 0:
            print("FIN: ", data_file," ", model_file, " cnt=", cnt)
    del d
    gc.collect()
    result_file_name = date_str+"-result"+"-"+str(model_idx)
    print("Dumping File ", result_file_name)
    with open(result_file_name, "w") as f:
        f.write(json.dumps(results))
        f.close()
    print("Dump Fin File ", result_file_name)
    cmd = "rm -f "+ data_file
    subprocess.call(cmd, shell=True)
    return 1

#python batch_model_test2.py --start-date 20191003
def wait_for_pre_process():
    while True:
        ret = subprocess.check_output("pgrep -f 'batch_model_test2.py'", shell=True)
        ret_arr =ret.decode().split("\n")
        if len(ret_arr) > 2:
            print("Wait ", ret_arr)
            time.sleep(10)
        else:
            break
def main():
    parser = argparse.ArgumentParser()                     
    parser.add_argument('--start-date', dest='start_date',
                        help='The start date of the model training')  
    parser.add_argument('--end-date', dest='end_date',
                        help='The end_date date of the model training')  
    args = parser.parse_args()    

    start_dt = datetime.strptime(args.start_date,"%Y%m%d")
    end_dt = datetime.strptime(args.end_date,"%Y%m%d")
    day_num = (end_dt - start_dt).days+1
    num_process = 10

    wait_for_pre_process()
    suites = []
    for i in range(0, day_num, num_process):
        date_item = start_dt + timedelta(days=i)
        if i + num_process < day_num:
            suites.append([date_item, num_process])
        else:
            suites.append([date_item, day_num-i+1])
    for suite in suites:
        start_dt = suite[0]
        day_num = suite[1]
        result_procs = []
        pool = Pool(processes= day_num)
        for i in range(day_num):
            date_item = start_dt + timedelta(days=i)
            print(date_item, " Start Processing File")
            result_procs.append(pool.apply_async(convert_data, args=(date_item,)))
        for result in result_procs:
            result.get()    
        pool.close()
        pool.join()
        print("Write Fin")
        pool = Pool(processes=2*FUTURE_CHUNKS)
        for i in range(0, day_num, 2):
            date_item = start_dt + timedelta(days=i)
            if i+1 <day_num:
                next_date_item = start_dt+timedelta(days=i+1)
            for j in range(FUTURE_CHUNKS):
                print(date_item," ", str(j), " Start Model Test")
                result_procs.append(pool.apply_async(model_test_on_one_portion, args=(date_item, j,)))
                if i+1 < day_num:
                    print(next_date_item," ", str(j), " Start Model Test")
                    result_procs.append(pool.apply_async(model_test_on_one_portion, args=(next_date_item, j,)))
            for result in result_procs:
                result.get()         
        pool.close()
        pool.join()







if __name__ == '__main__':
    main()

