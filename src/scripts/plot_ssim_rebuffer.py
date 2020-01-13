#!/usr/bin/env python3

import os
import sys
import argparse
import yaml
import json
import time
from datetime import datetime, timedelta
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

from helpers import (
    connect_to_influxdb, datetime_iter, ssim_index_to_db, get_ssim_index,
    get_abr_cc, query_measurement, retrieve_expt_config, connect_to_postgres)
from stream_processor import BufferStream


backup_hour = 11  # back up at 11 AM (UTC) every day
date_format = '%Y-%m-%dT%H:%M:%SZ'

args = None
expt = {}
influx_client = None
postgres_cursor = None

g_rebuffer = {}


def do_collect_ssim(s_str, e_str, d):
    sys.stderr.write('Processing video_acked data between {} and {}\n'
                     .format(s_str, e_str))
    video_acked_results = query_measurement(
        influx_client, 'video_acked', s_str, e_str)['video_acked']

    for pt in video_acked_results:
        expt_id = str(pt['expt_id'])
        expt_config = retrieve_expt_config(expt_id, expt, postgres_cursor)
        abr_cc = get_abr_cc(expt_config)

        if abr_cc not in d:
            d[abr_cc] = [0.0, 0]  # sum, count

        ssim_index = get_ssim_index(pt)
        if ssim_index is not None and ssim_index != 1:
            d[abr_cc][0] += ssim_index
            d[abr_cc][1] += 1


def collect_ssim():
    d = {}  # key: abr_cc; value: [sum, count]

    for s_str, e_str in datetime_iter(args.start_time, args.end_time):
        do_collect_ssim(s_str, e_str, d)

    # calculate average SSIM in dB
    for abr_cc in d:
        if d[abr_cc][1] == 0:
            sys.stderr.write('Warning: {} does not have SSIM data\n'
                             .format(abr_cc))
            continue

        avg_ssim_index = d[abr_cc][0] / d[abr_cc][1]
        avg_ssim_db = ssim_index_to_db(avg_ssim_index)
        d[abr_cc] = avg_ssim_db

    return d


def process_rebuffer_session(session, s):
    # exclude too short sessions
    if s['play_time'] < 5:
        return

    expt_id = str(session[-1])
    expt_config = retrieve_expt_config(expt_id, expt, postgres_cursor)
    abr_cc = get_abr_cc(expt_config)

    global g_rebuffer
    if abr_cc not in g_rebuffer:
        g_rebuffer[abr_cc] = {}
        g_rebuffer[abr_cc]['total_play'] = 0
        g_rebuffer[abr_cc]['total_rebuf'] = 0

    g_rebuffer[abr_cc]['total_play'] += s['play_time']
    g_rebuffer[abr_cc]['total_rebuf'] += s['cum_rebuf']


def collect_rebuffer():
    buffer_stream = BufferStream(process_rebuffer_session)
    buffer_stream.process(influx_client, args.start_time, args.end_time)

    return g_rebuffer


def plot_ssim_rebuffer(ssim, rebuffer):
    fig, ax = plt.subplots()
    title = '[{}, {}] (UTC)'.format(args.start_time, args.end_time)
    ax.set_title(title)
    ax.set_xlabel('Time spent stalled (%)')
    ax.set_ylabel('Average SSIM (dB)')
    ax.grid()

    for abr_cc in ssim:
        if abr_cc not in rebuffer:
            sys.stderr.write('Warning: {} does not exist in rebuffer\n'
                             .format(abr_cc))
            continue

        abr_cc_str = '{}+{}'.format(*abr_cc)

        total_rebuf = rebuffer[abr_cc]['total_rebuf']
        total_play = rebuffer[abr_cc]['total_play']
        rebuf_rate = total_rebuf / total_play

        abr_cc_str += '\n({:.1f}m/{:.1f}h)'.format(total_rebuf / 60,
                                                   total_play / 3600)

        x = rebuf_rate * 100  # %
        y = ssim[abr_cc]
        ax.scatter(x, y)
        ax.annotate(abr_cc_str, (x, y))

    # clamp x-axis to [0, 100]
    xmin, xmax = ax.get_xlim()
    xmin = max(xmin, 0)
    xmax = min(xmax, 100)
    ax.set_xlim(xmin, xmax)
    ax.invert_xaxis()

    output = args.output
    fig.savefig(output, dpi=150, bbox_inches='tight')
    sys.stderr.write('Saved plot to {}\n'.format(output))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('yaml_settings')
    parser.add_argument('--from', dest='start_time', required=True,
                        help='datetime in UTC conforming to RFC3339')
    parser.add_argument('--to', dest='end_time', required=True,
                        help='datetime in UTC conforming to RFC3339')
    parser.add_argument('--expt', help='e.g., expt_cache.json')
    parser.add_argument('-o', '--output', required=True)
    global args
    args = parser.parse_args()

    with open(args.yaml_settings, 'r') as fh:
        yaml_settings = yaml.safe_load(fh)

    if args.expt is not None:
        with open(args.expt, 'r') as fh:
            global expt
            expt = json.load(fh)
    else:
        # create a Postgres client and perform queries
        postgres_client = connect_to_postgres(yaml_settings)
        global postgres_cursor
        postgres_cursor = postgres_client.cursor()

    # create an InfluxDB client and perform queries
    global influx_client
    influx_client = connect_to_influxdb(yaml_settings)

    # collect ssim and rebuffer
    ssim = collect_ssim()
    rebuffer = collect_rebuffer()

    if not ssim or not rebuffer:
        sys.exit('Error: no data found in the queried range')

    print(ssim)
    print(rebuffer)

    # plot ssim vs rebuffer
    plot_ssim_rebuffer(ssim, rebuffer)

    if postgres_cursor:
        postgres_cursor.close()


if __name__ == '__main__':
    main()
