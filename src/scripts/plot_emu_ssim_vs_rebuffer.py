#!/usr/bin/env python3

import os
import sys
import argparse
import yaml
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

from helpers import (
    connect_to_influxdb, ssim_index_to_db, get_ssim_index,
    pretty_colors, pretty_names)
from collect_data import video_data_by_session, buffer_data_by_session


args = None


def plot_ssim_rebuffer(ssim, rebuffer):
    fig, ax = plt.subplots()
    ax.set_xlabel('Time spent stalled (%)')
    ax.set_ylabel('Average SSIM (dB)')

    ax.spines['right'].set_visible(False)
    ax.spines['top'].set_visible(False)

    for abr_cc in ssim:
        abr, cc = abr_cc

        total_rebuf = rebuffer[abr_cc]['total_rebuf']
        total_play = rebuffer[abr_cc]['total_play']
        rebuf_rate = total_rebuf / total_play

        x = rebuf_rate * 100  # %
        y = ssim[abr_cc]
        ax.scatter(x, y, color=pretty_colors[abr], clip_on=False)
        ax.annotate(pretty_names[abr], (x, y))

    # clamp x-axis to [0, 100]
    xmin, xmax = ax.get_xlim()
    xmin = max(xmin, 0)
    xmax = min(xmax, 100)
    ax.set_xlim(xmin, xmax)
    ax.invert_xaxis()

    fig.savefig(args.o, bbox_inches='tight')
    sys.stderr.write('Saved plot to {}\n'.format(args.o))


def collect_ssim(influx_client):
    video_acked_results = influx_client.query('SELECT * FROM video_acked')

    x = {}

    for pt in video_acked_results['video_acked']:
        abr_cc = tuple(pt['expt_id'].split('+'))
        if abr_cc not in x:
            x[abr_cc] = []

        ssim_index = get_ssim_index(pt)
        if ssim_index is not None:
            x[abr_cc].append(ssim_index)

    # calculate average SSIM in dB
    for abr_cc in x:
        avg_ssim_index = np.mean(x[abr_cc])
        avg_ssim_db = ssim_index_to_db(avg_ssim_index)
        x[abr_cc] = avg_ssim_db

    return x


def calculate_rebuffer_by_abr_cc(d):
    x = {}  # indexed by (abr, cc)

    for session in d:
        expt_id = session[-1]
        abr_cc = tuple(expt_id.split('+'))

        if abr_cc not in x:
            x[abr_cc] = {}
            x[abr_cc]['total_play'] = 0
            x[abr_cc]['total_rebuf'] = 0

        x[abr_cc]['total_play'] += d[session]['play']
        x[abr_cc]['total_rebuf'] += d[session]['rebuf']

    return x


def collect_rebuffer(influx_client):
    client_buffer_results = influx_client.query('SELECT * FROM client_buffer')

    buffer_data = buffer_data_by_session(client_buffer_results)

    rebuffer = calculate_rebuffer_by_abr_cc(buffer_data)

    return rebuffer


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('yaml_settings')
    parser.add_argument('-o', required=True)
    global args
    args = parser.parse_args()

    with open(args.yaml_settings, 'r') as fh:
        yaml_settings = yaml.safe_load(fh)

    # create an InfluxDB client and perform queries
    influx_client = connect_to_influxdb(yaml_settings)

    ssim = collect_ssim(influx_client)
    rebuffer = collect_rebuffer(influx_client)

    print(ssim)
    print(rebuffer)

    plot_ssim_rebuffer(ssim, rebuffer)


if __name__ == '__main__':
    main()
