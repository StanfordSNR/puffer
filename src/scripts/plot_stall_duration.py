#!/usr/bin/env python3

import os
from os import path
import sys
import argparse
import numpy as np
from scipy import stats

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib import ticker


args = None


def duration_to_bin(duration):
    return int(np.floor(np.log2(duration)))


def plot():
    # process data files
    max_bin_id = 16
    stalls = [[] for x in range(max_bin_id + 1)]

    for data_fname in os.listdir(args.i):
        if path.splitext(data_fname)[1] != '.csv':
            continue

        sys.stderr.write('Reading {}\n'.format(data_fname))
        data_path = path.join(args.i, data_fname)
        data_fh = open(data_path)

        for line in data_fh:
            (user, init_id, expt_id,
             play_time, cum_rebuf, startup_delay, num_rebuf) = line.split(',')

            play_time = float(play_time)
            if play_time < 1 or play_time >= 2 ** (max_bin_id + 1):
                continue
            cum_rebuf = float(cum_rebuf)

            bin_id = duration_to_bin(play_time)
            stalls[bin_id].append(cum_rebuf / play_time)

        data_fh.close()

    # prepare data for plotting
    x_data = []
    y_data = []
    y_errdata = []
    for i in range(max_bin_id + 1):
        x_data.append(2 ** i)
        y_data.append(np.mean(stalls[i]))
        y_errdata.append(stats.sem(stalls[i]))

    # plot
    fig, ax = plt.subplots()

    ax.errorbar(x_data, y_data, yerr=y_errdata, fmt='-o', markersize=3, capsize=3)

    ax.set_xlim(left=1)
    ax.set_xscale('log')
    ax.xaxis.set_major_formatter(ticker.FormatStrFormatter('%d'))

    ax.set_xlabel('Session duration (s)')
    ax.set_ylabel('Mean stall ratio')

    fig.savefig(args.output, bbox_inches='tight')
    sys.stderr.write('Saved plot to {}\n'.format(args.output))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('-i', help='folder of input data')
    parser.add_argument('-o', '--output', help='output figure', required=True)
    global args
    args = parser.parse_args()

    plot()


if __name__ == '__main__':
    main()
