#!/usr/bin/env python3

import os
import sys
import argparse
import yaml
from datetime import datetime
import numpy as np
from influxdb import InfluxDBClient
import matplotlib
import matplotlib.pyplot as plt


def connect_to_influxdb(yaml_settings):
    influx = yaml_settings['influxdb_connection']
    client = InfluxDBClient(influx['host'], influx['port'], influx['user'],
                            os.environ[influx['password']], influx['dbname'])
    sys.stderr.write('Connected to the InfluxDB at {}:{}\n'
                     .format(influx['host'], influx['port']))
    return client


def main():
    parser = argparse.ArgumentParser(
        'Run this script every hour to plot SSIM vs rebuffer rate')
    parser.add_argument('yaml_settings')
    parser.add_argument('--hours', type=int, default=1)
    args = parser.parse_args()

    with open(args.yaml_settings, 'r') as fh:
        yaml_settings = yaml.safe_load(fh)

    curr_ts = datetime.utcnow()

    # create an InfluxDB client and perform queries
    client = connect_to_influxdb(yaml_settings)
    client_video_result = client.query(
        'SELECT * FROM client_video WHERE time >= now() - {}h'.format(args.hours))
    client_buffer_result = client.query(
        'SELECT * FROM client_buffer WHERE time >= now() - {}h'.format(args.hours))

    time_str = "%Y-%m-%dT%H:%M:%S.%fZ"

    # TODO: retrieve experimental settings from Postgres

    # collect SSIMs
    ssim = {}
    for pt in client_video_result['client_video']:
        group_id = pt['group_id']

        if group_id not in ssim:
            ssim[group_id] = {}
            ssim[group_id]['ssim'] = []

        ssim_db = float(pt['ssim'])
        raw_ssim = 1 - 10 ** (ssim_db / -10)
        ssim[group_id]['ssim'].append(raw_ssim)

    # collect rebuffering times
    rebuffer = {}
    for pt in client_buffer_result['client_buffer']:
        group_id = pt['group_id']

        if group_id not in rebuffer:
            rebuffer[group_id] = {}

        session = (pt['user'], pt['init_id'])
        if session not in rebuffer[group_id]:
            rebuffer[group_id][session] = {}
            rebuffer[group_id][session]['min_time'] = None
            rebuffer[group_id][session]['max_time'] = None
            rebuffer[group_id][session]['min_cum_rebuf'] = None
            rebuffer[group_id][session]['max_cum_rebuf'] = None

        short = rebuffer[group_id][session]
        ts = datetime.strptime(pt['time'], time_str)
        cum_rebuf = float(pt['cum_rebuf'])

        if short['min_time'] is None or ts < short['min_time']:
            short['min_time'] = ts
        if short['max_time'] is None or ts > short['max_time']:
            short['max_time'] = ts

        if short['min_cum_rebuf'] is None or cum_rebuf < short['min_cum_rebuf']:
            short['min_cum_rebuf'] = cum_rebuf
        if short['max_cum_rebuf'] is None or cum_rebuf > short['max_cum_rebuf']:
            short['max_cum_rebuf'] = cum_rebuf

    data = {}  # data for plot
    for group_id in ssim:
        data[group_id] = {}

        # calculate average SSIM
        avg_raw_ssim = np.mean(ssim[group_id]['ssim'])
        avg_ssim = -10 * np.log10(1 - avg_raw_ssim)
        data[group_id]['avg_ssim'] = avg_ssim

        # calculate rebuffer rate
        total_play = 0
        total_rebuf = 0
        for session in rebuffer[group_id]:
            short = rebuffer[group_id][session]
            total_play += (short['max_time'] - short['min_time']).total_seconds()
            total_rebuf += short['max_cum_rebuf'] - short['min_cum_rebuf']

        if total_play == 0:
            exit('Error: total play time is 0')
        rebuf_rate = total_rebuf / total_play
        data[group_id]['rebuf_rate'] = rebuf_rate * 100

        print(group_id, avg_ssim, rebuf_rate)

    # TODO: map group_id to ABR + CC
    name_map = {
        '1': 'linear_bba + cubic',
        '2': 'linear_bba + bbr'
    }

    # plot
    curr_ts_str = curr_ts.strftime("%Y-%m-%dT%H")
    fig, ax = plt.subplots()
    ax.set_title('Performance in the last {}h of {} (UTC)'
                 .format(args.hours, curr_ts_str))
    ax.set_xlabel('Rebuffer rate (%)')
    ax.set_ylabel('Average SSIM (dB)')
    ax.grid()

    for group_id in data:
        x = data[group_id]['rebuf_rate']
        y = data[group_id]['avg_ssim']
        ax.scatter(x, y)
        ax.annotate(name_map[group_id], (x, y))

    fig.savefig(curr_ts_str + '.png', dpi=300,
                bbox_inches='tight', pad_inches=0.2)


if __name__ == '__main__':
    main()
