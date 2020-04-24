#!/usr/bin/env python3
# python ttp_local.py --use-csv --file-path ~/Documents/puffer-201903 --start-date 20190301 --end-date 20190301 --save-model ./save_model/
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
        # for test
        if args.use_debug and row_cnt >= 10000:
            break
    return rows
def process_raw_csv_data(video_sent_rows, video_acked_rows, cc):
    # calculate chunk transmission times
    print("process_raw_csv_data ", len(video_sent_rows), " ", len(video_acked_rows))
    d = {}
    last_video_ts = {}
    cnt = 0
    for row in video_sent_rows:
        pt = row_to_dict(row, VIDEO_SENT_KEYS)
        session = str(pt["session_id"])+ "|"+str(pt['channel_name'])+"|"+ str(pt['experiment_id'])
        # filter data points by congestion control
        if cc is not None and is_cc(pt["experiment_id"], cc):
            continue
        if session not in d:
            d[session] = {}
            last_video_ts[session] = None
        #print("chunk presentation time ",pt['chunk_presentation_timestamp'])
        video_ts = int(pt['chunk_presentation_timestamp'])
        if last_video_ts[session] is not None:
            if video_ts != last_video_ts[session] + VIDEO_DURATION:
                continue
        last_video_ts[session] = video_ts
        d[session][video_ts] = {}
        dsv = d[session][video_ts]  # short name
        ## debug
        #dsv['debug_session'] = session
        #dsv['sent_ts'] = np.datetime64(pt['timestamp'], 'ms')
        dsv['sent_ts'] = pt['timestamp']
        dsv['size'] = float(pt['chunk_size']) / PKT_BYTES  # bytes -> packets
        # byte/second -> packet/second
        dsv['delivery_rate'] = float(pt['delivery_rate']) / PKT_BYTES
        dsv['cwnd'] = float(pt['cwnd'])
        dsv['in_flight'] = float(pt['in_flight'])
        dsv['min_rtt'] = float(pt['min_rtt']) / MILLION  # us -> s
        dsv['rtt'] = float(pt['rtt']) / MILLION  # us -> s
        cnt += 1
        if cnt % 100000==0:
            print(" video_sent_rows cnt=",cnt)

    cnt = 0


    f = open("logge400", "w")

    for row in video_acked_rows:
        pt = row_to_dict(row, VIDEO_ACKED_KEYS)
        expt_id = pt['experiment_id']
        session = str(pt["session_id"])+ "|"+str(pt['channel_name'])+"|"+ str(pt['experiment_id'])
        # filter data points by congestion control
        if cc is not None and is_cc(expt_id, cc):
            continue
        if session not in d:
            continue
        video_ts = int(pt['chunk_presentation_timestamp'])
        if video_ts not in d[session]:
            continue
        dsv = d[session][video_ts]  # short name
        # calculate transmission time
        sent_ts = dsv['sent_ts']
        #acked_ts = np.datetime64(pt['timestamp'], 'ms')
        acked_ts =  pt['timestamp']
        dsv['acked_ts'] = acked_ts
        #dsv['trans_time'] = (acked_ts - sent_ts) / np.timedelta64(1, 's')
        dsv['trans_time'] = (acked_ts - sent_ts) / 1000
        
        if dsv['trans_time'] > 400:
            line = " "+ str(video_ts)+" "+ str(acked_ts)+ " "+str(sent_ts)+ " "+ str(dsv)
            f.write(line+"\n")
            print(">400 "," ", video_ts,  " ", acked_ts, " ", sent_ts, " ", dsv)
        cnt += 1
        if cnt % 100000==0:
            print(" video_acked_rows cnt=",cnt)

    return d

def read_and_write_csv_proc(proc_id, args, date_item, sample_size):
    print("io_proc ", proc_id)
    video_sent_file_name = args.file_path+'/'+ VIDEO_SENT_FILE_PREFIX + date_item.strftime('%Y-%m-%d') + FILE_SUFFIX
    video_acked_file_name = args.file_path+'/'+VIDEO_ACKED_FILE_PREFIX + date_item.strftime('%Y-%m-%d') + FILE_SUFFIX
    
    #client_buffer_file_name = CLIENT_BUFFER_FILE_PREFIX + date_item.strftime('%Y-%m-%d') + FILE_SUFFIX
    video_sent_rows =  read_csv_to_rows(args, video_sent_file_name)
    video_acked_rows = read_csv_to_rows(args, video_acked_file_name)
    print("io_proc ", proc_id, " ", len(video_sent_rows), " ", 
        len(video_acked_rows),  " read rows FIN next to process rawcsv ")
    raw_data = process_raw_csv_data(video_sent_rows, video_acked_rows, None)
    del video_sent_rows, video_acked_rows
    gc.collect()
    # collect input and output data from raw data
    raw_in_out = prepare_input_output(raw_data)
    del raw_data
    gc.collect()
    

    for i in range(len(raw_in_out)):
        in_file_name = args.output_path + '/'+date_item.strftime('%Y-%m-%d')+"-"+str(i) +".in"
        out_file_name = args.output_path + '/'+date_item.strftime('%Y-%m-%d')+"-"+str(i) +".out"
        '''
        with open(in_file_name) as in_file_obj:
            for line in in_file_obj:
                arr  = eval(line)
                print("leng " , len(arr))
                print(arr)
        with open(out_file_name) as out_file_obj:
            leng = 0
            for line in out_file_obj:
                arr  = eval(line)
                leng += len(arr)
            print("leng " , leng)
        '''
        
        in_file_obj = open(in_file_name, "w+")
        raw_in_items = raw_in_out[i]['in']
        raw_out_items = raw_in_out[i]['out']
        for input_data_item in raw_in_items:
            in_file_obj.write(str(input_data_item)+"\n")
        in_file_obj.close()
        print("FIN: ", in_file_name)
        
        out_file_obj = open(out_file_name, "w+")
        idx = 0
        while idx < len(raw_out_items):
            print("startng idx = ", idx)
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
    for i in range(len(key_list)):
        pt[key_list[i]] = row[i]
    return pt     
# to test whether this experiment_config uses cc 
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
    parser.add_argument('--use-debug', dest='use_debug', action='store_true', 
                        help='in debug mode')                        
    parser.add_argument('--file-path', dest='file_path',
                        help='path of training data')  
    parser.add_argument('--output-path', dest='output_path',
                        help='output path of training data')  
    parser.add_argument('--start-date', dest='start_date',
                        help='start date of the training data')  
    parser.add_argument('--end-date', dest='end_date',
                        help='end date of the training data')    

    args = parser.parse_args()
    print('file_path {0}'.format(args.file_path))
    print('start date {0}'.format(args.start_date)) 
    print('end date {0}'.format(args.end_date)) 

    start_dt = datetime.strptime(args.start_date,"%Y%m%d")
    end_dt = datetime.strptime(args.end_date,"%Y%m%d")
    day_num = (end_dt - start_dt).days+1

    #read_and_write_csv_proc(0, args, start_dt, None )


    partition = int(day_num /4)
    start_idx = 0
    end_idx = start_idx + partition
    for j in range(4):  
        start_idx = j* partition
        end_idx  = start_idx + partition
        if j ==3:
            end_idx = day_num
        print("startidx =", start_idx, " endidx=", end_idx)
        
        pool = Pool(processes= (end_idx-start_idx))
        result = [] 
        for i in range(start_idx, end_idx): 
        #for i in range(20, 21): 
            date_item = start_dt + timedelta(days=i)
            print(date_item)
            result.append(pool.apply_async(read_and_write_csv_proc, args=(i, args, date_item, None )))
        print("FIN Proce")
        for res in result:
            res.get()
        pool.close()
        pool.join()
        start_idx += partition
        end_idx = start_idx+ partition
    
        

if __name__ == '__main__':
    main()
