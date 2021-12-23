#!/usr/bin/env python3

import os
import sys
import argparse
import yaml
import copy
import json
import numpy as np
import pickle

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

from multiprocessing import Pool

from helpers import connect_to_influxdb, time_pair, datetime_iter_list
from stream_processor import VideoStream
from ttp import Model, PKT_BYTES
from ttp2 import Model as Linear_Model
from ttp_ab import Model as Abl_Model
from plot_helpers import *


VIDEO_DURATION = 180180
MID_SIZE = 766929
PAST_CHUNKS = 8
PLOT_POINTS = 10000

args = None
cnt = 0
cnt_2 = 0

def process_session(session, s, tputs, tput_diffs):
    pre = None
    for curr_ts in s:
        curr = s[curr_ts]['size'] / s[curr_ts]['trans_time'] # bytes/sec
        tputs.append(curr)
        if pre != None:
            tput_diffs.append(abs(curr - pre))
        pre = curr


def worker(start_time, end_time):
    tputs = []
    tput_diffs = []
    expt = None

    with open(args.yaml_settings, 'r') as fh:
        yaml_settings = yaml.safe_load(fh)

    with open(args.expt, 'r') as fh:
        expt = json.load(fh)

    # create an InfluxDB client and perform queries
    influx_client = connect_to_influxdb(yaml_settings)

    video_stream = VideoStream(
            lambda session, s, tputs=tputs, tput_diffs=tput_diffs:
                process_session(session, s, tputs, tput_diffs))
    video_stream.process(influx_client, start_time, end_time)

    return tputs, tput_diffs

def save_distr(distr):
    distr.sort()
    tot = len(distr)

    if tot < PLOT_POINTS:
        return distr

    samples = [distr[round(i * tot / PLOT_POINTS)] \
                        for i in range(PLOT_POINTS)]
    return samples

def plot_tput_distr(tputs, start_time, end_time):
    global cnt
    cnt += 1

    fig, ax = plt.subplots()
    title = 'Throughputs Distribution [{}, {}] (UTC)'\
            .format(start_time, end_time)
    ax.set_title(title)
    ax.set_xlabel('Throughputs (Bytes/Sec)')
    ax.set_ylabel('Percentage (%)')
    ax.grid()

    tputs.sort()
    tot = len(tputs)
    tputs_rate = [100 * i / tot for i in range(tot)]

    ax.semilogx(tputs, tputs_rate)

    output = args.output + '_' + str(cnt) + '.svg'
    fig.savefig(output, dpi=150, bbox_inches='tight')
    sys.stderr.write('Saved plot to {}\n'.format(output))


def plot_diff_distr(tput_diffs, start_time, end_time):
    global cnt_2
    cnt_2 += 1

    fig, ax = plt.subplots()
    title = 'Throughputs Changing Rate Distribution [{}, {}] (UTC)'\
            .format(start_time, end_time)
    ax.set_title(title)
    ax.set_xlabel('Throughputs Changing (%)')
    ax.set_ylabel('Percentage (%)')
    ax.grid()

    tput_diffs.sort()
    tot = len(tput_diffs)
    tput_diffs = [diff * 100 for diff in tput_diffs]
    diffs_rate = [100 * i / tot for i in range(tot)]

    ax.semilogx(tput_diffs, diffs_rate)

    output = args.output + '_d_' + str(cnt_2) + '.svg'
    fig.savefig(output, dpi=150, bbox_inches='tight')
    sys.stderr.write('Saved plot to {}\n'.format(output))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('yaml_settings')
    parser.add_argument('-t', action='append', type=time_pair, required=True)
    parser.add_argument('--expt', help='e.g., expt_cache.json')
    parser.add_argument('-o', '--output', required=True)
    parser.add_argument('-d', '--delta', required=True)
    global args
    args = parser.parse_args()


    pool = Pool(processes=12)
    procs = []

    start_time = None

    for s_str, e_str in datetime_iter_list(args.t, int(args.delta)):
        procs.append((pool.apply_async(worker, (s_str, e_str,)),
                      s_str, e_str))
        if start_time == None:
            start_time = s_str

    tputs = []
    tput_diffs = []

    for proc, s_str, e_str in procs:
        _tputs, _tput_diffs = proc.get()
        tputs += _tputs
        tput_diffs += _tput_diffs

        plot_tput_distr(tputs, start_time, e_str)
        plot_diff_distr(tput_diffs, start_time, e_str)

    with open(args.output + '.pickle', 'wb') as fh:
        pickle.dump((save_distr(tputs), save_distr(tput_diffs)), fh)


if __name__ == '__main__':
    main()
