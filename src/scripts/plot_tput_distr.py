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
from matplotlib.ticker import FuncFormatter

VIDEO_DURATION = 180180
MID_SIZE = 766929
PAST_CHUNKS = 8
PLOT_POINTS = 10000
MBPS = 1024 * 1024 / 8

args = None

labels = ['Real world', 'FCC traces']

colors = ['C0', 'C1']
linestyles = ['solid', 'dashed']

formatter = FuncFormatter(lambda y, _: '{:.16g}'.format(y))

def plot_tput_distr(tputs):
    fig, ax = plt.subplots()
    ax.set_xlabel('Throughput (Mbps)')
    ax.set_ylabel('CDF')

    for i in range(2):
        tot = len(tputs[i])
        tputs[i] = [tput / MBPS for tput in tputs[i]]
        tputs_rate = [i / tot for i in range(tot)]

        ax.semilogx(tputs[i], tputs_rate, label=labels[i],
                    color=colors[i], linestyle=linestyles[i])

    ax.legend()
    ax.set_ylim(0,1)
    ax.set_xlim(0.1)

    ax.xaxis.set_major_formatter(formatter)
    output = args.output + '.svg'
    fig.savefig(output, bbox_inches='tight')
    sys.stderr.write('Saved plot to {}\n'.format(output))


def plot_diff_distr(tput_diffs):
    fig, ax = plt.subplots()
    title = 'Throughputs Changing Distribution'
    ax.set_title(title)
    ax.set_xlabel('Throughputs Changing (Mbps)')
    ax.set_ylabel('CDF')
    ax.grid()

    for i in range(2):
        tot = len(tput_diffs[i])
        tput_diffs[i] = [diff / MBPS for diff in tput_diffs[i]]
        diffs_rate = [i / tot for i in range(tot)]

        ax.semilogx(tput_diffs[i], diffs_rate, label=labels[i])

    ax.legend()

    output = args.output + '_d.svg'
    fig.savefig(output, dpi=150, bbox_inches='tight')
    sys.stderr.write('Saved plot to {}\n'.format(output))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('-o', '--output', required=True)
    global args
    args = parser.parse_args()

    tputs = []
    tput_diffs = []

    with open('plot_tput_distr/real.pickle', 'rb') as fh:
        real_tputs, real_diffs = pickle.load(fh)
        tputs.append(real_tputs)
        tput_diffs.append(real_diffs)

    with open('plot_tput_distr/eml.pickle', 'rb') as fh:
        eml_tputs, eml_diffs = pickle.load(fh)
        tputs.append(eml_tputs)
        tput_diffs.append(eml_diffs)

    plot_tput_distr(tputs)
    plot_diff_distr(tput_diffs)


if __name__ == '__main__':
    main()
