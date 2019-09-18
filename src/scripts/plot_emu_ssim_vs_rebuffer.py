#!/usr/bin/env python3

import os
from os import path
import sys
import argparse
import yaml
import json

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

from helpers import (
    connect_to_influxdb, ssim_index_to_db, pretty_colors, pretty_names)
from collect_data import video_data_by_session, buffer_data_by_session


args = None
influx_client = None


def get_video_data():
    video_sent_results = influx_client.query('SELECT * FROM video_sent')
    video_acked_results = influx_client.query('SELECT * FROM video_acked')
    video_data = video_data_by_session(video_sent_results, video_acked_results)

    for session in video_data:
        ssim_index_ctr = [0.0, 0]  # sum, count
        for ts in video_data[session]:
            a = video_data[session][ts]  # short name
            ssim_index_ctr[0] += a['ssim_index']
            ssim_index_ctr[1] += 1
        video_data[session] = ssim_index_ctr[0] / ssim_index_ctr[1]

    return video_data


def get_buffer_data():
    client_buffer_results = influx_client.query('SELECT * FROM client_buffer')
    buffer_data = buffer_data_by_session(client_buffer_results)

    return buffer_data


def get_data():
    video_data = get_video_data()
    buffer_data = get_buffer_data()

    # merge common sessions
    common_sessions = set(video_data) & set(buffer_data)

    data = {}
    for session in sorted(common_sessions, key=lambda s: int(s[1])):
        expt_id = session[-1]

        abr, cc = tuple(expt_id.split('+'))
        if cc != 'bbr':
            continue

        if abr not in data:
            # weighted SSIM (index) sum, total stall, total watch time
            data[abr] = [0.0, 0.0, 0.0]

        # short names
        vs = video_data[session]
        bs = buffer_data[session]

        data[abr][0] += vs * bs['play']
        data[abr][1] += bs['rebuf']
        data[abr][2] += bs['play']

    for abr in data:
        avg_ssim_db = ssim_index_to_db(data[abr][0] / data[abr][2])
        stall_ratio = data[abr][1] / data[abr][2]

        data[abr] = [stall_ratio, avg_ssim_db]

    return data


def plot_ssim_rebuffer(data):
    fig, ax = plt.subplots()

    for abr in data:
        x = data[abr][0] * 100  # -> %
        y = data[abr][1]

        ax.scatter(x, y, color=pretty_colors[abr], clip_on=False)
        ax.annotate(pretty_names[abr], (x, y))

    # clamp x-axis to [0, 100]
    xmin, xmax = ax.get_xlim()
    xmin = max(xmin, 0)
    xmax = min(xmax, 100)
    ax.set_xlim(xmin, xmax)
    ax.invert_xaxis()

    ax.set_xlabel('Time spent stalled (%)')
    ax.set_ylabel('Average SSIM (dB)')

    ax.spines['right'].set_visible(False)
    ax.spines['top'].set_visible(False)

    fig.savefig(args.o, bbox_inches='tight')
    sys.stderr.write('Saved plot to {}\n'.format(args.o))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('yaml_settings', nargs='?')
    parser.add_argument('-o', required=True)
    parser.add_argument('-i', help='input JSON data')
    global args
    args = parser.parse_args()

    if args.i:
        with open(args.i) as fh:
            data = json.load(fh)
            plot_ssim_rebuffer(data)
        return

    with open(args.yaml_settings, 'r') as fh:
        yaml_settings = yaml.safe_load(fh)

    # create an InfluxDB client and perform queries
    global influx_client
    influx_client = connect_to_influxdb(yaml_settings)

    data = get_data()

    # dump data to .json
    json_file = path.splitext(args.o)[0] + '.json'
    with open(json_file, 'w') as fh:
        json.dump(data, fh)
    sys.stderr.write('Dumped data to {}\n'.format(json_file))

    plot_ssim_rebuffer(data)


if __name__ == '__main__':
    main()
