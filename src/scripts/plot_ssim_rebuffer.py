#!/usr/bin/env python3

import os
import sys
import argparse
import yaml
from datetime import datetime, timedelta
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

from helpers import connect_to_postgres, connect_to_influxdb, try_parsing_time


# cache of Postgres data: experiment 'id' -> json 'data' of the experiment
expt_id_cache = {}


def retrieve_expt_config(postgres_cursor, expt_id):
    if expt_id not in expt_id_cache:
        postgres_cursor.execute(
            'SELECT * FROM puffer_experiment WHERE id={};'.format(expt_id))
        rows = postgres_cursor.fetchall()
        if len(rows) != 1:
            sys.exit('Error: invalid experiment ID {}'.format(expt_id))

        expt_id_cache[expt_id] = rows[0][2]

    return expt_id_cache[expt_id]


def collect_ssim(video_acked_results, postgres_cursor):
    # process InfluxDB data
    x = {}
    for pt in video_acked_results['video_acked']:
        expt_id = pt['expt_id']
        expt_config = retrieve_expt_config(postgres_cursor, expt_id)
        # index x by (abr, cc)
        abr_cc = (expt_config['abr'], expt_config['cc'])
        if abr_cc not in x:
            x[abr_cc] = []

        if pt['ssim_index'] is not None:
            ssim_index = float(pt['ssim_index'])
            x[abr_cc].append(ssim_index)

    # calculate average SSIM in dB
    ssim = {}
    for abr_cc in x:
        avg_ssim_index = np.mean(x[abr_cc])
        if avg_ssim_index == 1:
            sys.exit('Error: average SSIM index is 1')
        avg_ssim_db = -10 * np.log10(1 - avg_ssim_index)
        ssim[abr_cc] = avg_ssim_db

    return ssim


def collect_rebuffer(client_buffer_results, postgres_cursor):
    # process InfluxDB data
    x = {}
    for pt in client_buffer_results['client_buffer']:
        expt_id = pt['expt_id']
        expt_config = retrieve_expt_config(postgres_cursor, expt_id)
        # index x by (abr, cc)
        abr_cc = (expt_config['abr'], expt_config['cc'])
        if abr_cc not in x:
            x[abr_cc] = {}

        session = (pt['user'], pt['init_id'])
        if session not in x[abr_cc]:
            x[abr_cc][session] = {}
            x[abr_cc][session]['min_time'] = None
            x[abr_cc][session]['max_time'] = None
            x[abr_cc][session]['min_cum_rebuf'] = None
            x[abr_cc][session]['max_cum_rebuf'] = None

        # shorthand variable
        y = x[abr_cc][session]
        ts = try_parsing_time(pt['time'])
        cum_rebuf = float(pt['cum_rebuf'])

        if y['min_time'] is None or ts < y['min_time']:
            y['min_time'] = ts
        if y['max_time'] is None or ts > y['max_time']:
            y['max_time'] = ts

        if y['min_cum_rebuf'] is None or cum_rebuf < y['min_cum_rebuf']:
            y['min_cum_rebuf'] = cum_rebuf
        if y['max_cum_rebuf'] is None or cum_rebuf > y['max_cum_rebuf']:
            y['max_cum_rebuf'] = cum_rebuf

    # calculate rebuffer rate
    rebuffer = {}
    for abr_cc in x:
        total_play = 0
        total_rebuf = 0

        for session in x[abr_cc]:
            # shorthand variable
            y = x[abr_cc][session]
            total_play += (y['max_time'] - y['min_time']).total_seconds()
            total_rebuf += y['max_cum_rebuf'] - y['min_cum_rebuf']

        if total_play == 0:
            sys.exit('Error: total play time is 0')

        rebuf_rate = total_rebuf / total_play
        rebuffer[abr_cc] = rebuf_rate * 100

    return rebuffer


def plot_ssim_rebuffer(ssim, rebuffer, output, days):
    time_str = '%Y-%m-%d'
    curr_ts = datetime.utcnow()
    start_ts = curr_ts - timedelta(days=days)
    curr_ts_str = curr_ts.strftime(time_str)
    start_ts_str = start_ts.strftime(time_str)

    title = ('Performance in [{}, {}) (UTC)'
             .format(start_ts_str, curr_ts_str))

    fig, ax = plt.subplots()
    ax.set_title(title)
    ax.set_xlabel('Time spent stalled (%)')
    ax.set_ylabel('Average SSIM (dB)')
    ax.grid()

    for abr_cc in ssim:
        abr_cc_str = '{}+{}'.format(*abr_cc)
        if abr_cc not in rebuffer:
            sys.exit('Error: {} does not exist both ssim and rebuffer'
                     .format(abr_cc_str))

        x = rebuffer[abr_cc]
        y = ssim[abr_cc]
        ax.scatter(x, y)
        ax.annotate(abr_cc_str, (x, y))

    # clamp x-axis to [0, 100]
    xmin, xmax = ax.get_xlim()
    xmin = max(xmin, 0)
    xmax = min(xmax, 100)
    ax.set_xlim(xmin, xmax)
    ax.invert_xaxis()

    fig.savefig(output, dpi=300, bbox_inches='tight', pad_inches=0.2)
    sys.stderr.write('Saved plot to {}\n'.format(output))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('yaml_settings')
    parser.add_argument('-o', '--output', required=True)
    parser.add_argument('-d', '--days', type=int, default=1)
    args = parser.parse_args()
    output = args.output
    days = args.days

    if days < 1:
        sys.exit('-d/--days must be a positive integer')

    with open(args.yaml_settings, 'r') as fh:
        yaml_settings = yaml.safe_load(fh)

    # create an InfluxDB client and perform queries
    influx_client = connect_to_influxdb(yaml_settings)

    # query video_acked and client_buffer
    video_acked_results = influx_client.query(
        'SELECT * FROM video_acked WHERE time >= now() - {}d'.format(days))
    client_buffer_results = influx_client.query(
        'SELECT * FROM client_buffer WHERE time >= now() - {}d'.format(days))

    # create a Postgres client and perform queries
    postgres_client = connect_to_postgres(yaml_settings)
    postgres_cursor = postgres_client.cursor()

    # collect ssim and rebuffer
    ssim = collect_ssim(video_acked_results, postgres_cursor)
    rebuffer = collect_rebuffer(client_buffer_results, postgres_cursor)

    if not ssim or not rebuffer:
        sys.exit('Error: no data found in the queried range')

    # plot ssim vs rebuffer
    plot_ssim_rebuffer(ssim, rebuffer, output, days)

    postgres_cursor.close()


if __name__ == '__main__':
    main()
