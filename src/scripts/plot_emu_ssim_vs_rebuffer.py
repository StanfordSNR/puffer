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
    connect_to_influxdb, ssim_index_to_db, get_ssim_index)
from collect_data import video_data_by_session, buffer_data_by_session


def plot_ssim_rebuffer(ssim, rebuffer):
    fig, ax = plt.subplots()
    ax.set_xlabel('Time spent stalled (%)')
    ax.set_ylabel('Average SSIM (dB)')
    ax.grid()

    for abr_cc in ssim:
        abr_cc_str = '{}+{}'.format(*abr_cc)
        if abr_cc not in rebuffer:
            sys.exit('Error: {} does not exist in both ssim and rebuffer'
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

    output = 'emu_ssim_vs_rebuffer.png'
    fig.savefig(output, dpi=150, bbox_inches='tight')
    sys.stderr.write('Saved plot to {}\n'.format(output))


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
    args = parser.parse_args()

    with open(args.yaml_settings, 'r') as fh:
        yaml_settings = yaml.safe_load(fh)

    # create an InfluxDB client and perform queries
    influx_client = connect_to_influxdb(yaml_settings)

    ssim = collect_ssim(influx_client)

    rebuffer = collect_rebuffer(influx_client)

    plot_ssim_rebuffer(ssim, rebuffer)


if __name__ == '__main__':
    main()
