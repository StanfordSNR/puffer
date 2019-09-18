#!/usr/bin/env python3

import os
from os import path
import sys
import argparse
import yaml
import numpy as np
from multiprocessing import Pool

from helpers import (
    connect_to_influxdb, time_pair, datetime_iter_list, ssim_index_to_db)
from stream_processor import VideoStream, VIDEO_DURATION


args = None


def save_session(session, s, data_fh):
    first_ssim_index = None
    min_rtt = None

    primary_cnt = 0
    ssim_index_sum = 0.0
    ssim_db_sum = 0.0
    delivery_rate_sum = 0.0
    throughput_sum = 0.0
    rtt_sum = 0.0

    diff_cnt = 0
    ssim_db_diff_sum = 0.0
    prev_ssim_db = None

    ts = min(s.keys())
    while ts in s:
        # get ssim_index and ssim_db
        ssim_index = s[ts]['ssim_index']
        if ssim_index == 1:
            ts += VIDEO_DURATION
            prev_ssim_db = None
            continue
        ssim_db = ssim_index_to_db(ssim_index)

        if first_ssim_index is None:
            first_ssim_index = ssim_index

        if min_rtt is None or s[ts]['min_rtt'] < min_rtt:
            min_rtt = s[ts]['min_rtt']

        primary_cnt += 1
        ssim_index_sum += ssim_index
        ssim_db_sum += ssim_db
        delivery_rate_sum += s[ts]['delivery_rate']
        throughput_sum += s[ts]['size'] / s[ts]['trans_time']
        rtt_sum += s[ts]['rtt']

        if prev_ssim_db is not None:
            ssim_db_diff_sum += abs(ssim_db - prev_ssim_db)
            diff_cnt += 1
        prev_ssim_db = ssim_db

        ts += VIDEO_DURATION

    if primary_cnt == 0 or first_ssim_index is None or min_rtt is None:
        return

    avg_ssim_index = ssim_index_sum / primary_cnt
    avg_ssim_db = ssim_db_sum / primary_cnt
    avg_delivery_rate = delivery_rate_sum / primary_cnt
    avg_throughput = throughput_sum / primary_cnt
    avg_rtt = rtt_sum / primary_cnt

    if diff_cnt == 0:
        avg_ssim_db_diff = 0
    else:
        avg_ssim_db_diff = ssim_db_diff_sum / diff_cnt

    # user, init_id, expt_id, first_ssim_index, min_rtt,
    # primary_cnt, avg_ssim_index, avg_ssim_db,
    # avg_delivery_rate, avg_throughput, avg_rtt,
    # diff_cnt, avg_ssim_db_diff
    line = list(session)
    line += [first_ssim_index, min_rtt,
             primary_cnt, avg_ssim_index, avg_ssim_db,
             avg_delivery_rate, avg_throughput, avg_rtt,
             diff_cnt, avg_ssim_db_diff]

    data_fh.write('{},{:d},{:d},{:.6f},{:.6f},'
                  '{:d},{:.6f},{:.6f},{:.6f},{:.6f},{:.6f},'
                  '{:d},{:.6f}\n'.format(*line))


def worker(s_str, e_str):
    with open(args.yaml_settings, 'r') as fh:
        yaml_settings = yaml.safe_load(fh)

    # create an InfluxDB client and perform queries
    influx_client = connect_to_influxdb(yaml_settings)

    data_path = path.join(args.o, '{}_{}.csv'.format(s_str, e_str))
    sys.stderr.write('Saving video data to {}\n'.format(data_path))

    data_fh = open(data_path, 'w')

    video_stream = VideoStream(
        lambda session, s, data_fh=data_fh: save_session(session, s, data_fh))
    video_stream.process(influx_client, s_str, e_str)

    data_fh.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('yaml_settings')
    parser.add_argument('-t', action='append', type=time_pair, required=True)
    parser.add_argument('-o', help='output directory of data', required=True)
    global args
    args = parser.parse_args()

    if not path.isdir(args.o):
        os.makedirs(args.o)

    pool = Pool()
    procs = []

    for s_str, e_str in datetime_iter_list(args.t):
        procs.append(pool.apply_async(worker, (s_str, e_str,)))

    for proc in procs:
        proc.wait()


if __name__ == '__main__':
    main()
