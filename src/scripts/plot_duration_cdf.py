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

from helpers import retrieve_expt_config, get_abr_cc
from plot_helpers import abr_order, pretty_name, pretty_color, pretty_linestyle


args = None
expt = {}


def get_session_duration():
    duration = {}  # { abr_cc: [ durations ] }

    # read session durations from data files
    for data_fname in os.listdir(args.i):
        if path.splitext(data_fname)[1] != '.csv':
            continue

        sys.stderr.write('Reading {}\n'.format(data_fname))
        data_path = path.join(args.i, data_fname)
        data_fh = open(data_path)

        root = {}  # { session: root_session }
        mega = {}  # { root_session: total play_time }

        for line in data_fh:
            (user, init_id, expt_id,
             play_time, cum_rebuf, startup_delay, num_rebuf) = line.split(',')

            play_time = float(play_time)
            session = (user, int(init_id), int(expt_id))

            expt_config = retrieve_expt_config(expt_id, expt, None)
            abr_cc = get_abr_cc(expt_config)

            abr, cc = abr_cc
            if abr not in abr_order or cc != 'bbr':
                continue

            if not args.mega:
                if abr_cc not in duration:
                    duration[abr_cc] = []

                if play_time >= 1:  # exclude sessions with duration < 1s
                    duration[abr_cc].append(play_time)
                continue

            # if args.mega, then find root
            session_prev = (user, int(init_id) - 1, int(expt_id))
            if session_prev in root:
                root[session] = root[session_prev]
            else:
                assert(session not in root)
                root[session] = session

            if root[session] not in mega:
                mega[root[session]] = 0
            mega[root[session]] += play_time

        data_fh.close()

        if args.mega:
            for session in mega:
                expt_config = retrieve_expt_config(str(session[-1]), expt, None)
                abr_cc = get_abr_cc(expt_config)

                if abr_cc not in duration:
                    duration[abr_cc] = []
                if mega[session] >= 1:  # exclude sessions with duration < 1s
                    duration[abr_cc].append(mega[session])

    return duration


def get_plot_data(duration):
    # convert duration to data for plotting (CDF)
    plot_data = {}

    # find max duration
    max_duration = 0
    for abr_cc in duration:
        md = np.max(duration[abr_cc])
        if md > max_duration:
            max_duration = md

    for abr_cc in duration:
        # output stats
        mean_seconds = np.mean(duration[abr_cc])
        sem_seconds = stats.sem(duration[abr_cc])
        print('{}, {:.3f}, {:.3f}'.format(abr_cc, mean_seconds, sem_seconds))

        # log-scale histogram
        counts, bin_edges = np.histogram(
            duration[abr_cc], np.logspace(0, np.log10(max_duration), 100))

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
    parser.add_argument('--expt', help='e.g., expt_cache.json')
    parser.add_argument('-o', '--output', help='output figure', required=True)
    parser.add_argument('--mega', action='store_true')
    global args
    args = parser.parse_args()

    with open(args.expt) as fh:
        global expt
        expt = json.load(fh)

    duration = get_session_duration()
    plot_data = get_plot_data(duration)
    plot_session_duration(plot_data)


if __name__ == '__main__':
    main()
