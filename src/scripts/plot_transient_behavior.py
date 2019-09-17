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

from helpers import retrieve_expt_config, get_abr_cc, ssim_index_to_db
from plot_helpers import abr_order, pretty_name, pretty_color, pretty_linestyle


args = None
expt = {}


def get_data():
    data = {}  # { abr_cc: [[], []] }

    for data_fname in os.listdir(args.data):
        if path.splitext(data_fname)[1] != '.csv':
            continue

        data_path = path.join(args.data, data_fname)
        data_fh = open(data_path)

        root = {}  # { session: root_session }

        for line in data_fh:
            (user, init_id, expt_id, first_ssim_index,
             avg_ssim_index, avg_ssim_db_diff, avg_delivery_rate, avg_tput,
             play_time, cum_rebuf, startup_delay, num_rebuf) = line.split(',')

            session = (user, int(init_id), int(expt_id))
            first_ssim_index = float(first_ssim_index)
            startup_delay = float(startup_delay)

            expt_config = retrieve_expt_config(expt_id, expt, None)
            abr_cc = get_abr_cc(expt_config)

            abr, cc = abr_cc
            if abr not in abr_order or cc != 'bbr':
                continue

            if abr_cc not in data:
                data[abr_cc] = [[], []]

            if args.channel_change:
                data[abr_cc][0].append(first_ssim_index)
                data[abr_cc][1].append(startup_delay)
                continue

            # if not args.channel_change, then find root
            assert(session not in root)
            is_channel_change = False
            for i in range(1, 11):
                # some init_ids might be missing
                session_prev = (user, int(init_id) - i, int(expt_id))
                if session_prev in root:  # channel change
                    is_channel_change = True
                    root[session] = root[session_prev]
                    break

            if not is_channel_change:  # cold start
                root[session] = session
                data[abr_cc][0].append(first_ssim_index)
                data[abr_cc][1].append(startup_delay)

        data_fh.close()

    for abr_cc in data:
        for i in range(2):
            a = data[abr_cc][i]
            mean_a = np.mean(a)
            ci_a = st.t.interval(0.95, len(a) - 1, loc=mean_a, scale=st.sem(a))

            if i == 0:  # ssim
                data[abr_cc][i] = (ssim_index_to_db(mean_a),
                                   ssim_index_to_db(ci_a[0]),
                                   ssim_index_to_db(ci_a[1]))
            else:  # delay
                data[abr_cc][i] = (mean_a, ci_a[0], ci_a[1])

    return data


def plot(data):
    fig, ax = plt.subplots()

    for abr_cc in data:
        abr, cc = abr_cc
        x, x_minus, x_plus = data[abr_cc][1]
        y, y_minus, y_plus = data[abr_cc][0]

        ax.errorbar(x, y,
                    xerr=[[x_plus-x], [x-x_minus]],
                    yerr=[[y_plus-y], [y-y_minus]],
                    fmt='-o', markersize=3, capsize=3,
                    color=pretty_color[abr])
        ax.annotate(pretty_name[abr], (x, y))

    ax.invert_xaxis()

    ax.spines['top'].set_visible(False)
    ax.spines['right'].set_visible(False)

    ax.set_xlabel('Startup delay (s)')
    ax.set_ylabel('First chunk SSIM (dB)')

    fig.savefig(args.o)
    sys.stderr.write('Saved plot to {}\n'.format(args.o))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--data', help='folder of data')
    parser.add_argument('--expt', help='e.g., expt_cache.json')
    parser.add_argument('-o', help='output figure', required=True)
    parser.add_argument('--channel-change', action='store_true')
    global args
    args = parser.parse_args()

    with open(args.expt) as fh:
        global expt
        expt = json.load(fh)

    data = get_data()
    plot(data)


if __name__ == '__main__':
    main()
