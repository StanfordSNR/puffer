#!/usr/bin/env python3

import sys
import argparse
import yaml
import json
import numpy as np

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib import ticker

from helpers import (
    connect_to_influxdb, retrieve_expt_config, get_abr_cc)
from stream_processor import BufferStream
from plot_helpers import abr_order, pretty_name, pretty_color, pretty_linestyle


args = None
influx_client = None
expt = {}

g_duration = {}  # { abr_cc: [] }


def process_session(session, s):
    expt_id = str(session[-1])
    expt_config = retrieve_expt_config(expt_id, expt, None)
    abr_cc = get_abr_cc(expt_config)

    global g_duration
    if abr_cc not in g_duration:
        g_duration[abr_cc] = []

    g_duration[abr_cc].append(s['play_time'])


def collect_session_duration():
    buffer_stream = BufferStream(process_session, no_outliers=False)
    buffer_stream.process(influx_client, args.start_time, args.end_time)

    plot_data = {}

    for abr_cc in g_duration:
        counts, bin_edges = np.histogram(g_duration[abr_cc], bins=100,)

        x = bin_edges
        y = np.cumsum(counts) / len(g_duration[abr_cc])
        y = np.insert(y, 0, 0)  # prepend 0

        plot_data[','.join(abr_cc)] = [list(x), list(y)]

    return plot_data


def plot_session_duration(plot_data):
    fig, ax = plt.subplots()

    for abr in abr_order:
        abr_cc = abr + ',bbr'
        if abr_cc not in plot_data:
            sys.stderr.write('Warning: {} does not exist\n'.format(abr_cc))

        ax.plot(plot_data[abr_cc][0], plot_data[abr_cc][1],
                label=pretty_name[abr], color=pretty_color[abr],
                linestyle=pretty_linestyle[abr])

    ax.set_xlim(1)
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
    parser.add_argument('yaml_settings', nargs='?')
    parser.add_argument('--from', dest='start_time',
                        help='datetime in UTC conforming to RFC3339')
    parser.add_argument('--to', dest='end_time',
                        help='datetime in UTC conforming to RFC3339')
    parser.add_argument('--expt', default='expt_cache.json',
                        help='e.g., expt_cache.json')
    parser.add_argument('-o', '--output', help='output figure', required=True)
    parser.add_argument('--data-out', help='JSON file to output data for plot')
    parser.add_argument('--data-in', help='JSON file of input data for plot')
    global args
    args = parser.parse_args()

    if args.data_in:
        with open(args.data_in) as fh:
            plot_data = json.load(fh)
        plot_session_duration(plot_data)
        return

    with open(args.yaml_settings) as fh:
        yaml_settings = yaml.safe_load(fh)

    # create an InfluxDB client and perform queries
    global influx_client
    influx_client = connect_to_influxdb(yaml_settings)

    with open(args.expt) as fh:
        global expt
        expt = json.load(fh)

    plot_data = collect_session_duration()
    with open(args.data_out, 'w') as fh:
        json.dump(plot_data, fh)
    plot_session_duration(plot_data)


if __name__ == '__main__':
    main()
