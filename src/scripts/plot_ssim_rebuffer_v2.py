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
    get_abr_cc, query_measurement)
from stream_processor import StreamProcessor


backup_hour = 11  # back up at 11 AM (UTC) every day
date_format = '%Y-%m-%dT%H:%M:%SZ'

args = None


def do_collect_ssim(influx_client, expt, s_str, e_str, d):
    print('Processing video_acked data between {} and {}'.format(
          s_str, e_str))
    video_acked_results = query_measurement(influx_client, 'video_acked',
                                            s_str, e_str)

    for pt in video_acked_results['video_acked']:
        expt_id = str(pt['expt_id'])
        expt_config = expt[expt_id]

        abr_cc = get_abr_cc(expt_config)
        if abr_cc not in d:
            d[abr_cc] = [0.0, 0]  # sum, count

        ssim_index = get_ssim_index(pt)
        if ssim_index is not None:
            d[abr_cc][0] += ssim_index
            d[abr_cc][1] += 1


def collect_ssim(influx_client, expt):
    d = {}

    for s_str, e_str in datetime_iter(args.start_time, args.end_time):
        do_collect_ssim(influx_client, expt, s_str, e_str, d)

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


def do_collect_rebuffer(influx_client, expt, s_str, e_str, stream_processor):
    print('Processing client_buffer data between {} and {}'.format(
          s_str, e_str))
    client_buffer_results = query_measurement(influx_client, 'client_buffer',
                                              s_str, e_str)

    for pt in client_buffer_results['client_buffer']:
        stream_processor.add_data_point(pt)


def collect_rebuffer(influx_client, expt):
    stream_processor = StreamProcessor(expt)

    for s_str, e_str in datetime_iter(args.start_time, args.end_time):
        do_collect_rebuffer(influx_client, expt, s_str, e_str, stream_processor)

    return stream_processor.out


def plot_ssim_rebuffer(ssim, rebuffer):
    fig, ax = plt.subplots()
    title = '[{}, {}] (UTC)'.format(args.start_time, args.end_time)
    ax.set_title(title)
    ax.set_xlabel('Time spent stalled (%)')
    ax.set_ylabel('Average SSIM (dB)')
    ax.grid()

    for abr_cc in ssim:
        abr_cc_str = '{}+{}'.format(*abr_cc)
        if abr_cc not in rebuffer:
            sys.exit('Error: {} does not exist in rebuffer'
                     .format(abr_cc))

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
    parser.add_argument('--from', dest='start_time',
                        help='datetime in UTC conforming to RFC3339')
    parser.add_argument('--to', dest='end_time',
                        help='datetime in UTC conforming to RFC3339')
    parser.add_argument('--expt', help='e.g., expt_cache.json')
    parser.add_argument('-o', '--output', required=True)
    global args
    args = parser.parse_args()

    with open(args.yaml_settings, 'r') as fh:
        yaml_settings = yaml.safe_load(fh)

    with open(args.expt, 'r') as fh:
        expt = json.load(fh)

    # create an InfluxDB client and perform queries
    influx_client = connect_to_influxdb(yaml_settings)

    # collect ssim and rebuffer
    ssim = collect_ssim(influx_client, expt)
    rebuffer = collect_rebuffer(influx_client, expt)

    if not ssim or not rebuffer:
        sys.exit('Error: no data found in the queried range')

    # plot ssim vs rebuffer
    plot_ssim_rebuffer(ssim, rebuffer)


if __name__ == '__main__':
    main()
