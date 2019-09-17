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
    ssim_index_ctr = [0.0, 0]  # sum, count
    ssim_db_diff_ctr = [0.0, 0]  # sum, count
    prev_ssim_db = None
    delivery_rate_ctr = [0.0, 0]  # sum, count
    tput_ctr = [0.0, 0]  # sum, count

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

        ssim_index_ctr[0] += ssim_index
        ssim_index_ctr[1] += 1

        if prev_ssim_db is not None:
            ssim_db_diff_ctr[0] += abs(ssim_db - prev_ssim_db)
            ssim_db_diff_ctr[1] += 1
        prev_ssim_db = ssim_db

        delivery_rate_ctr[0] += s[ts]['delivery_rate']
        delivery_rate_ctr[1] += 1

        tput_ctr[0] += s[ts]['size'] / s[ts]['trans_time']
        tput_ctr[1] += 1

        ts += VIDEO_DURATION

    if (first_ssim_index is None
        or ssim_index_ctr[1] == 0
        or ssim_db_diff_ctr[1] == 0
        or delivery_rate_ctr[1] == 0
        or tput_ctr[1] == 0):
        return

    avg_ssim_index = ssim_index_ctr[0] / ssim_index_ctr[1]
    avg_ssim_db_diff = ssim_db_diff_ctr[0] / ssim_db_diff_ctr[1]
    avg_delivery_rate = delivery_rate_ctr[0] / delivery_rate_ctr[1]
    avg_tput = tput_ctr[0] / tput_ctr[1]

    # user, init_id, expt_id, first_ssim_index,
    # avg_ssim_index, avg_ssim_db_diff, avg_delivery_rate, avg_tput
    line = list(session)
    line.append(first_ssim_index)
    line.append(avg_ssim_index)
    line.append(avg_ssim_db_diff)
    line.append(avg_delivery_rate)
    line.append(avg_tput)

    data_fh.write('{},{:d},{:d},{:.6f},{:.6f},{:.6f},{:.3f},{:.3f}\n'
                  .format(*line))


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
