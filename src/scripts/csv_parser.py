#!/usr/bin/env python3
# To execute the script, suppose you have downloaded puffer-201903.tar.gz 
# and uncompress it to the path  ~/Documents/puffer-201903/ 
# If you want to parse the data between 0325 and 0330, which corresponds to 10 csv files
# (i.e. video_sent_2019-03-25T11.csv/video_acked_2019-03-25T11.csv ~ video_sent_2019-03-30T11.csv/video_acked_2019-03-30T11.csv),
# you will execute the following commands, after it completes the parsing, you can obtain the training data at 
# the output-path ~/Documents/fork_puffer/training_data_foler, which contains 5 .in files and 5 .out files
# for each day (e.g. for video_sent_2019-03-25T11.csv/video_acked_2019-03-25T11.csv, it generates 2019-03-25-0.in/2019-03-25-0.in ~2019-03-25-4.in/2019-03-25-4.in),
# which can be used to train 5 TTPs in parallel.  
# python csv_parser.py --file-path ~/Documents/puffer-201903/ --output-path ~/Documents/fork_puffer/training_data_foler --start-date 20190325 --end-date 20190330
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

from helpers import (
    connect_to_influxdb, connect_to_postgres,
    make_sure_path_exists, retrieve_expt_config, create_time_clause,
    get_expt_id, get_user)

VIDEO_SENT_FILE_PREFIX = 'video_sent_'
VIDEO_ACKED_FILE_PREFIX = 'video_acked_'
CLIENT_BUFFER_FILE_PREFIX = 'client_buffer_'
FILE_SUFFIX = 'T11.csv'
FILE_CHUNK_SIZE = 10000
VIDEO_SENT_KEYS=['timestamp', 'session_id',
'experiment_id', 'channel_name', 'chunk_presentation_timestamp', 'resolution',
'chunk_size', 'ssim_index',	'cwnd', 'in_flight', 'min_rtt','rtt','delivery_rate']
VIDEO_ACKED_KEYS=['timestamp', 'session_id',
'experiment_id', 'channel_name', 'chunk_presentation_timestamp']
CLIENT_BUFFER_KEYS=['timestamp', 'session_id',	
'experiment_id', 'channel_name', 'event', 'playback_buffer', 'cumulative_rebuffer']
PAST_CHUNKS = 8
FUTURE_CHUNKS = 5
DIM_IN = 62

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

# Read CSV files (either video_sent_XXXX-XX-XXT11.csv or video_acked_XXXX-XX-XXT11.csv) 
# to memory. The file is large, so use chunk-based read operation provided by pandas
def read_csv_to_rows(args, data_file):
    merge_dt = pd.read_csv( data_file,  
                            header=None, encoding="utf_8", engine='python' , 
                            iterator = True, chunksize=FILE_CHUNK_SIZE ) 
    rows = []
    row_cnt = 0
    for chunk in merge_dt:
        for index, row in chunk.iterrows():              
            rows.append(row)
        row_cnt += chunk.shape[0]
        print(data_file +' row_cnt=', row_cnt)
        # if row_cnt >= 50000:
        #     break
    return rows
# Process the pair of sent_rows and acked_rows to generate training data. 
def process_raw_csv_data(video_sent_rows, video_acked_rows, cc):
    #skip the header
    video_sent_rows = video_sent_rows[1:]
    video_acked_rows = video_acked_rows[1:]
    # calculate chunk transmission times
    print("process_raw_csv_data ", len(video_sent_rows), " ", len(video_acked_rows))
    d = {}
    last_video_ts = {}
    cnt = 0
    for row in video_sent_rows:
        pt = row_to_dict(row, VIDEO_SENT_KEYS)
        # session_id + channel_name + experiment_id can uniquely identify one data point
        session = str(pt["session_id"])+ "|"+str(pt['channel_name'])+"|"+ str(pt['experiment_id'])
        # Filter data points by congestion control
        # If cc is not specified (i.e. cc is None), we just parse all the data, regardless of the
        # congestion control they use.
        if cc is not None and is_cc(pt["experiment_id"], cc):
            continue
        # Ignore those incomplete data points
        if session not in d:
            d[session] = {}
            last_video_ts[session] = None
        video_ts = int(pt['chunk_presentation_timestamp'])
        if last_video_ts[session] is not None:
            if video_ts != last_video_ts[session] + VIDEO_DURATION:
                continue
        last_video_ts[session] = video_ts
        d[session][video_ts] = {}
        dsv = d[session][video_ts]  # short name
        dsv['sent_ts'] = pt['timestamp']
        # bytes -> packets 
        dsv['size'] = float(pt['chunk_size']) / PKT_BYTES  
        # byte/second -> packet/second
        dsv['delivery_rate'] = float(pt['delivery_rate']) / PKT_BYTES
        dsv['cwnd'] = float(pt['cwnd'])
        dsv['in_flight'] = float(pt['in_flight'])
        dsv['min_rtt'] = float(pt['min_rtt']) / MILLION  # us -> s
        dsv['rtt'] = float(pt['rtt']) / MILLION  # us -> s
        cnt += 1
        if cnt % 10000==0:
            print(" video_sent_rows cnt=",cnt)

    cnt = 0
    for row in video_acked_rows:
        pt = row_to_dict(row, VIDEO_ACKED_KEYS)
        expt_id = pt['experiment_id']
        session = str(pt["session_id"])+ "|"+str(pt['channel_name'])+"|"+ str(pt['experiment_id'])
        # Filter data points by congestion control
        if cc is not None and is_cc(expt_id, cc):
            continue
        if session not in d:
            continue
        video_ts = int(pt['chunk_presentation_timestamp'])
        if video_ts not in d[session]:
            continue
        dsv = d[session][video_ts]  # short name
        # Calculate transmission time, and convert it from milliseconds to seconds
        sent_ts = int(dsv['sent_ts'])
        acked_ts = int(pt['timestamp'])
        dsv['acked_ts'] = acked_ts
        dsv['trans_time'] = (acked_ts - sent_ts) / 1000 # milliseconds to seconds
        cnt += 1
        if cnt % 10000==0:
            print(" video_acked_rows cnt=",cnt)
    return d
# Read the CSV files and generate .in/.out files
# For each pait of CSV files, it iwll generate FUTURE_CHUNKS pairs of .in/.out files
def read_and_write_csv_proc(proc_id, args, date_item):
    print("read_and_write_csv_proc: ", proc_id)
    # Construct the corresponding file name
    video_sent_file_name = args.file_path+'/'+ VIDEO_SENT_FILE_PREFIX + date_item.strftime('%Y-%m-%d') + FILE_SUFFIX
    video_acked_file_name = args.file_path+'/'+VIDEO_ACKED_FILE_PREFIX + date_item.strftime('%Y-%m-%d') + FILE_SUFFIX
    video_sent_rows =  read_csv_to_rows(args, video_sent_file_name)
    video_acked_rows = read_csv_to_rows(args, video_acked_file_name)
    print("read_and_write_csv_proc ", proc_id, " ", len(video_sent_rows), " ", 
        len(video_acked_rows),  " complete reading rows, will process the raw csv ")
    raw_data = process_raw_csv_data(video_sent_rows, video_acked_rows, None)
    # Forcefully free these memory for future use (video_sent_rows and video_acked_rows take a lot of memory)
    del video_sent_rows, video_acked_rows
    gc.collect()
    # Collect input and output data from raw data
    raw_in_out = prepare_input_output(raw_data)
    # Forcefully free these memory for future use
    del raw_data
    gc.collect()
    
    for i in range(len(raw_in_out)):
        in_file_name = args.output_path + '/'+date_item.strftime('%Y-%m-%d')+"-"+str(i) +".in"
        out_file_name = args.output_path + '/'+date_item.strftime('%Y-%m-%d')+"-"+str(i) +".out"        
        in_file_obj = open(in_file_name, "w+")
        raw_in_items = raw_in_out[i]['in']
        raw_out_items = raw_in_out[i]['out']
        for input_data_item in raw_in_items:
            in_file_obj.write(str(input_data_item)+"\n")
        in_file_obj.close()
        print("FIN: ", in_file_name)
        out_file_obj = open(out_file_name, "w")
        idx = 0
        # Since there is only 1 output value for each data point,we write 10000 values for each line
        # to save some space
        while idx < len(raw_out_items):
            sub = raw_out_items[idx:idx+10000]
            out_file_obj.write(str(sub)+"\n")
            idx += 10000
        out_file_obj.close()
        print("FIN: ", out_file_name)
    del raw_in_out
    gc.collect()
    print("Write Fin ", in_file_name, " ",out_file_name )
    return 0



def row_to_dict(row, key_list):
    pt = {}
    #print(key_list)
    for i in range(len(key_list)):
        pt[key_list[i]] = row[i]
    return pt     
# To test whether this experiment_config uses cc 
# Since I include all training data without considering their congestion control.
# Simply return True in this function. But you can also add other filtering. 
def is_cc(experiment_id, cc):
    return True

def append_past_chunks(ds, next_ts, row):
    i = 1
    past_chunks = []
    while i <= PAST_CHUNKS:
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

    if i != PAST_CHUNKS + 1:  # break in the middle; padding must exist
        while i <= PAST_CHUNKS:
            past_chunks = padding + past_chunks
            i += 1

    row += past_chunks

# Create the training data (i.e. 62-dimensional vector for each data point, 
# representing the information of one video chunk) and training label (i.e.
# the transmission time of this video chunk) 
# return FUTURE_CHUNKS pairs of (raw_in, raw_out)
def prepare_input_output(d):
    ret = [{'in':[], 'out':[]} for _ in range(FUTURE_CHUNKS)]
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
            for i in range(FUTURE_CHUNKS):
                row_i = row.copy()
                ts = next_ts + i * VIDEO_DURATION
                if ts in ds and 'trans_time' in ds[ts]:
                    row_i += [ds[ts]['size']]
                    assert(len(row_i) == DIM_IN)
                    ret[i]['in'].append(row_i)
                    ret[i]['out'].append(ds[ts]['trans_time'])

    return ret


def main():
    parser = argparse.ArgumentParser()                     
    parser.add_argument('--file-path', dest='file_path',
                        help='path of the CSV Files (i.e. video_sent_XXXX-XX-XXT11.csv or video_acked_XXXX-XX-XXT11.csv)')  
    parser.add_argument('--output-path', dest='output_path',
                        help='output path of processed training data (i.e. .in and .out files)')  

    # By specifying the start-date and end-date, we will construct the file name of the CSV files based 
    # on that, and parse them into .in and .out files for training. 
    parser.add_argument('--start-date', dest='start_date',
                        help='start date of the training data (e.g.20190301)')  
    parser.add_argument('--end-date', dest='end_date',
                        help='end date of the training data')    

    args = parser.parse_args()
    print('file_path {0}'.format(args.file_path))
    print('output_path {0}'.format(args.output_path))
    print('start date {0}'.format(args.start_date)) 
    print('end date {0}'.format(args.end_date)) 

    start_dt = datetime.strptime(args.start_date,"%Y%m%d")
    end_dt = datetime.strptime(args.end_date,"%Y%m%d")
    day_num = (end_dt - start_dt).days+1

    # Use multiple processing to accelerate the parsing process, 
    # You can tune num_processes based on your machine performance
    num_processes = 4
    pool = Pool(processes= num_processes)
    results = []
    for i in range(day_num):
        date_item = start_dt + timedelta(days=i)
        print(date_item)
        results.append(pool.apply_async(read_and_write_csv_proc, args=(i, args, date_item )))
    for result in results:
        result.get()

    print("Finished")
    
        

if __name__ == '__main__':
    main()


# python batch_model_test.py --start-date 20190126 --end-date 20190430
# python batch_model_test.py --start-date 20190501 --end-date 20190731
# python batch_model_test.py --start-date 20190801 --end-date 20191030
# python batch_model_test.py --start-date 20191101 --end-date 20200131
# python batch_model_test.py --start-date 20200201 --end-date 20200430