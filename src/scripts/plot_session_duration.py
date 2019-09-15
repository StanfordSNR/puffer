#!/usr/bin/env python3

import os
from os import path
import sys
import argparse
import yaml
import json
import numpy as np
from scipy import stats

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib import ticker

from helpers import (
    connect_to_influxdb, retrieve_expt_config, get_abr_cc, time_pair)
from stream_processor import BufferStream
from plot_helpers import abr_order, pretty_name, pretty_color, pretty_linestyle


args = None
expt = {}


def collect_session_duration():
    # read session durations from data files
    duration = {}

    for data_fname in os.listdir(args.i):
        sys.stderr.write('Reading {}\n'.format(data_fname))
        data_path = path.join(args.i, data_fname)
        data_fh = open(data_path)

        for line in data_fh:
            (user, init_id, expt_id,
             play_time, cum_rebuf, startup_delay, num_rebuf) = line.split(',')

            play_time = float(play_time)
            if play_time < 1:
                continue

            expt_config = retrieve_expt_config(expt_id, expt, None)
            abr_cc = get_abr_cc(expt_config)

            abr, cc = abr_cc
            if abr not in abr_order or cc != 'bbr':
                continue

            if abr_cc not in duration:
                duration[abr_cc] = []

            duration[abr_cc].append(play_time)

        data_fh.close()

    # convert duration to data for plotting (CDF)
    plot_data = {}

    for abr_cc in duration:
        # output stats
        mean_seconds = np.mean(duration[abr_cc])
        sem_seconds = stats.sem(duration[abr_cc])
        print('{}, {:.3f}, {:.3f}'.format(abr_cc, mean_seconds, sem_seconds))

        counts, bin_edges = np.histogram(duration[abr_cc], bins=100)

        x = bin_edges
        y = np.cumsum(counts) / len(duration[abr_cc])
        y = np.insert(y, 0, 0)  # prepend 0

        plot_data[abr_cc] = [x, y]

    return plot_data


def plot_session_duration(plot_data):
    fig, ax = plt.subplots()

    for abr in abr_order:
        abr_cc = (abr, 'bbr')
        if abr_cc not in plot_data:
            sys.stderr.write('Warning: {} does not exist\n'.format(abr_cc))
            continue

        ax.plot(plot_data[abr_cc][0], plot_data[abr_cc][1],
                label=pretty_name[abr], color=pretty_color[abr],
                linestyle=pretty_linestyle[abr])

    ax.set_xlim(left=1)
    ax.set_xscale('log')
    ax.xaxis.set_major_formatter(ticker.FormatStrFormatter('%d'))

    ax.set_ylim(0, 1)

    ax.legend(loc='lower right')
    ax.set_xlabel('Session duration (s)')
    ax.set_ylabel('CDF')

    fig.savefig(args.output, bbox_inches='tight')
    sys.stderr.write('Saved plot to {}\n'.format(args.output))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('-i', help='folder of input data')
    parser.add_argument('--expt', default='expt_cache.json',
                        help='expt_cache.json by default')
    parser.add_argument('-o', '--output', help='output figure', required=True)
    global args
    args = parser.parse_args()

    with open(args.expt) as fh:
        global expt
        expt = json.load(fh)

    plot_data = collect_session_duration()
    plot_session_duration(plot_data)


if __name__ == '__main__':
    main()
