#!/usr/bin/env python3

import os
from os import path
import sys
import argparse
import yaml
import json
import numpy as np
import scipy.stats as st

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib import ticker

from helpers import (
    retrieve_expt_config, get_abr_cc, ssim_index_to_db, parse_line)
from plot_helpers import abr_order, pretty_name, pretty_color, pretty_linestyle


args = None
expt = {}

max_rtt = 450
bin_size = 50
bin_max = int(max_rtt / bin_size)

def rtt_to_bin(rtt):
    return int(np.floor(rtt / bin_size))


def get_data():
    data = {}

    for data_fname in os.listdir(args.data):
        if path.splitext(data_fname)[1] != '.csv':
            continue

        data_path = path.join(args.data, data_fname)
        data_fh = open(data_path)

        for line in data_fh:
            (user, init_id, expt_id, first_ssim_index, min_rtt,
             primary_cnt, avg_ssim_index, avg_ssim_db,
             avg_delivery_rate, avg_throughput, avg_rtt,
             diff_cnt, avg_ssim_db_diff,
             play_time, cum_rebuf, startup_delay, num_rebuf) = parse_line(line)

            session = (user, init_id, expt_id)

            expt_config = retrieve_expt_config(expt_id, expt, None)
            abr_cc = get_abr_cc(expt_config)

            abr, cc = abr_cc
            if abr not in abr_order or cc != 'bbr':
                continue

            if abr_cc not in data:
                if args.yaxis == 'ssim':
                    data[abr_cc] = [[] for i in range(bin_max)]
                else:
                    data[abr_cc] = [[] for i in range(bin_max)]

            bin_id = min(max(rtt_to_bin(avg_rtt * 1000), 0), bin_max - 1)

            if args.yaxis == 'ssim':
                data[abr_cc][bin_id].append(avg_ssim_index)
            elif args.yaxis == 'stall':
                if play_time >= 4:
                    data[abr_cc][bin_id].append(cum_rebuf / play_time)

        data_fh.close()

    for abr_cc in data:
        for i in range(bin_max):
            a = data[abr_cc][i]
            mean_a = np.mean(a)

            if args.yaxis == 'ssim':
                data[abr_cc][i] = ssim_index_to_db(mean_a)
            elif args.yaxis == 'stall':
                data[abr_cc][i] = mean_a

    return data


def plot(data):
    fig, ax = plt.subplots()

    for abr_cc in data:
        abr, cc = abr_cc

        x = np.array(range(bin_max)) * bin_size
        y = []
        for i in range(bin_max):
            y.append(data[abr_cc][i])

        ax.plot(x, y,
                color=pretty_color[abr], label=pretty_name[abr])

    ax.legend()

    ax.spines['top'].set_visible(False)
    ax.spines['right'].set_visible(False)

    ax.set_xlabel('Min RTT (ms)')

    if args.yaxis == 'ssim':
        ax.set_ylabel('Average SSIM (dB)')
    elif args.yaxis == 'stall':
        ax.set_ylabel('Time spent stalled (%)')

    fig.savefig(args.o)
    sys.stderr.write('Saved plot to {}\n'.format(args.o))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--data', help='folder of data')
    parser.add_argument('--expt', help='e.g., expt_cache.json')
    parser.add_argument('-o', help='output figure', required=True)
    parser.add_argument('--yaxis', choices=['ssim', 'stall'])
    global args
    args = parser.parse_args()

    with open(args.expt) as fh:
        global expt
        expt = json.load(fh)

    data = get_data()
    plot(data)


if __name__ == '__main__':
    main()
