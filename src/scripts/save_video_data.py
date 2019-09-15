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
    chunk_cnt = 0
    ssim_index_sum = 0.0
    ssim_db_sum = 0.0
    ssim_index_diff_sum = 0.0
    ssim_db_diff_sum = 0.0

    prev_ssim_index = None
    prev_ssim_db = None

    ts = min(s.keys())
    while ts in s:
        # get ssim_index and ssim_db
        ssim_index = s[ts]['ssim_index']
        if ssim_index == 1:
            ts += VIDEO_DURATION
            continue
        ssim_db = ssim_index_to_db(ssim_index)

        chunk_cnt += 1
        ssim_index_sum += ssim_index
        ssim_db_sum += ssim_db
        if chunk_cnt > 1:
            ssim_index_diff_sum += abs(ssim_index - prev_ssim_index)
            ssim_db_diff_sum += abs(ssim_db - prev_ssim_db)

        prev_ssim_index = ssim_index
        prev_ssim_db = ssim_db

        ts += VIDEO_DURATION

    # user, init_id, expt_id, chunk_cnt,
    # ssim_index_sum, ssim_db_sum, ssim_index_diff_sum, ssim_db_diff_sum
    line = list(session)
    line.append(chunk_cnt)
    line.append(ssim_index_sum)
    line.append(ssim_db_sum)
    line.append(ssim_index_diff_sum)
    line.append(ssim_db_diff_sum)

    data_fh.write('{},{:d},{:d},{:d},{:.6f},{:.6f},{:.6f},{:.6f}\n'
                  .format(*line))


def worker(s_str, e_str):
    with open(args.yaml_settings, 'r') as fh:
        yaml_settings = yaml.safe_load(fh)

    # create an InfluxDB client and perform queries
    influx_client = connect_to_influxdb(yaml_settings)

    data_path = path.join(args.out_dir, '{}_{}.txt'.format(s_str, e_str))
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
    parser.add_argument('-o', '--out-dir', help='output directory of data',
                        required=True)
    global args
    args = parser.parse_args()

    if not path.isdir(args.out_dir):
        os.makedirs(args.out_dir)

    pool = Pool()
    procs = []

    for s_str, e_str in datetime_iter_list(args.t):
        worker(s_str, e_str)
        # procs.append(pool.apply_async(worker, (s_str, e_str,)))

    for proc in procs:
        proc.wait()


if __name__ == '__main__':
    main()
