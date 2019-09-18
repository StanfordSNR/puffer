#!/usr/bin/env python3

import os
from os import path
import sys
import argparse
import json
import numpy as np
import scipy.stats as st

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

from helpers import (
    retrieve_expt_config, get_abr_cc, ssim_index_to_db, parse_line,
    datetime_iter_list, time_pair)
from plot_helpers import abr_order, pretty_name, pretty_color
emu_abr_order = abr_order + ['puffer_ttp_emu']


args = None
expt = {}


def get_data():
    data = {}  # { abr_cc: [[ list of avg SSIM ], [ list of stall ratio ]] }

    for s_str, e_str in datetime_iter_list(args.t):
        data_fname = '{}_{}.csv'.format(s_str, e_str)
        data_path = path.join(args.data, data_fname)
        data_fh = open(data_path)

        for line in data_fh:
            (user, init_id, expt_id, first_ssim_index,
             avg_ssim_index, avg_ssim_db_diff, avg_delivery_rate, avg_tput,
             play_time, cum_rebuf, startup_delay, num_rebuf) = parse_line(line)

            if play_time < 4:
                continue

            session = (user, init_id, expt_id)

            expt_config = retrieve_expt_config(expt_id, expt, None)
            abr_cc = get_abr_cc(expt_config)

            abr, cc = abr_cc
            if abr not in emu_abr_order or cc != 'bbr':
                continue

            if abr_cc not in data:
                data[abr_cc] = [[], []]

            data[abr_cc][0].append(avg_ssim_index)
            data[abr_cc][1].append(cum_rebuf / play_time)

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
            else:  # stall ratio
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
    ax.set_xlabel('Time spent stalled (%)')
    ax.set_ylabel('Average SSIM (dB)')

    fig.savefig(args.o)
    sys.stderr.write('Saved plot to {}\n'.format(args.o))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--data', help='folder of data')
    parser.add_argument('-t', action='append', type=time_pair, required=True)
    # -t 2019-01-26T08:00:00Z,2019-04-03T08:00:00Z
    parser.add_argument('--expt', help='e.g., expt_cache.json')
    parser.add_argument('-o', help='output figure', required=True)
    global args
    args = parser.parse_args()

    with open(args.expt) as fh:
        global expt
        expt = json.load(fh)

    data = get_data()
    plot(data)


if __name__ == '__main__':
    main()
