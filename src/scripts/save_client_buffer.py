#!/usr/bin/env python3

import os
from os import path
import sys
import argparse
import yaml
import numpy as np
from multiprocessing import Pool

from helpers import connect_to_influxdb, time_pair, datetime_iter_list
from stream_processor import BufferStream


args = None


def save_session(session, s, data_fh):
    # user, init_id, expt_id, play_time, cum_rebuf, startup_delay, num_rebuf
    line = list(session)
    line.append(s['play_time'])
    line.append(s['cum_rebuf'])
    line.append(s['startup_delay'])
    line.append(s['num_rebuf'])

    data_fh.write('{},{:d},{:d},{:.3f},{:.3f},{:.3f},{:d}\n'.format(*line))


def worker(s_str, e_str):
    with open(args.yaml_settings, 'r') as fh:
        yaml_settings = yaml.safe_load(fh)

    # create an InfluxDB client and perform queries
    influx_client = connect_to_influxdb(yaml_settings)

    data_path = path.join(args.out_dir, '{}_{}.txt'.format(s_str, e_str))
    sys.stderr.write('Saving client_buffer data to {}\n'.format(data_path))

    data_fh = open(data_path, 'w')

    buffer_stream = BufferStream(
        lambda session, s, data_fh=data_fh: save_session(session, s, data_fh))
    buffer_stream.process(influx_client, s_str, e_str)

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
        procs.append(pool.apply_async(worker, (s_str, e_str,)))

    for proc in procs:
        proc.wait()


if __name__ == '__main__':
    main()
