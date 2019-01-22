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

from helpers import (
    connect_to_postgres, connect_to_influxdb, try_parsing_time,
    ssim_index_to_db, retrieve_expt_config, create_time_clause, get_ssim_index)


# cache of Postgres data: experiment 'id' -> json 'data' of the experiment
expt_id_cache = {}


def collect_ssim(video_acked_results, postgres_cursor):
    # process InfluxDB data
    x = {}
    for pt in video_acked_results['video_acked']:
        expt_id = int(pt['expt_id'])
        expt_config = retrieve_expt_config(expt_id, expt_id_cache,
                                           postgres_cursor)
        # index x by (abr, cc)
        abr_cc = (expt_config['abr'], expt_config['cc'])
        if abr_cc not in x:
            x[abr_cc] = []

        ssim_index = get_ssim_index(pt)
        if ssim_index is not None:
            x[abr_cc].append(ssim_index)

    # calculate average SSIM in dB
    ssim = {}
    for abr_cc in x:
        avg_ssim_index = np.mean(x[abr_cc])
        avg_ssim_db = ssim_index_to_db(avg_ssim_index)
        ssim[abr_cc] = avg_ssim_db

    return ssim


def collect_rebuffer(client_buffer_results, postgres_cursor):
    # process InfluxDB data
    x = {}
    for pt in client_buffer_results['client_buffer']:
        expt_id = int(pt['expt_id'])
        expt_config = retrieve_expt_config(expt_id, expt_id_cache,
                                           postgres_cursor)
        # index x by (abr, cc)
        abr_cc = (expt_config['abr'], expt_config['cc'])
        if abr_cc not in x:
            x[abr_cc] = {}

        # index x[abr_cc] by session
        session = (pt['user'], int(pt['init_id']),
                   pt['channel'], int(pt['expt_id']))
        if session not in x[abr_cc]:
            x[abr_cc][session] = {}
            x[abr_cc][session]['min_play_time'] = None
            x[abr_cc][session]['max_play_time'] = None
            x[abr_cc][session]['min_cum_rebuf'] = None
            x[abr_cc][session]['max_cum_rebuf'] = None

        y = x[abr_cc][session]  # short name

        ts = try_parsing_time(pt['time'])
        cum_rebuf = float(pt['cum_rebuf'])

        if pt['event'] == 'startup':
            y['min_play_time'] = ts
            y['min_cum_rebuf'] = cum_rebuf

        if y['max_play_time'] is None or ts > y['max_play_time']:
            y['max_play_time'] = ts

        if y['max_cum_rebuf'] is None or cum_rebuf > y['max_cum_rebuf']:
            y['max_cum_rebuf'] = cum_rebuf

    # calculate rebuffer rate
    rebuffer = {}
    total_play = {}
    total_rebuf = {}

    for abr_cc in x:
        abr_cc_play = 0
        abr_cc_rebuf = 0

        for session in x[abr_cc]:
            y = x[abr_cc][session]  # short name

            if y['min_play_time'] is None or y['min_cum_rebuf'] is None:
                continue

            sess_play = (y['max_play_time'] - y['min_play_time']).total_seconds()
            sess_rebuf = y['max_cum_rebuf'] - y['min_cum_rebuf']

            # exclude too short sessions
            if sess_play < 2:
                continue

            # TODO: identify and ignore outliers
            if sess_rebuf / sess_play > 0.5:
                continue

            abr_cc_play += sess_play
            abr_cc_rebuf += sess_rebuf

        if abr_cc_play == 0:
            sys.exit('Error: {}: total play time is 0'.format(abr_cc))

        total_play[abr_cc] = abr_cc_play
        total_rebuf[abr_cc] = abr_cc_rebuf

        rebuf_rate = abr_cc_rebuf / abr_cc_play
        rebuffer[abr_cc] = rebuf_rate * 100

    return rebuffer, total_play, total_rebuf


def plot_ssim_rebuffer(ssim, rebuffer, total_play, total_rebuf, output,
                       time_start, time_end):
    fig, ax = plt.subplots()

    title = '[{}, {}] (UTC)'.format(time_start, time_end)
    ax.set_title(title)
    ax.set_xlabel('Time spent stalled (%)')
    ax.set_ylabel('Average SSIM (dB)')
    ax.grid()

    for abr_cc in ssim:
        abr_cc_str = '{}+{}'.format(*abr_cc)
        if abr_cc not in rebuffer:
            sys.exit('Error: {} does not exist both ssim and rebuffer'
                     .format(abr_cc_str))

        abr_cc_str += '\n({:.1f}h/{:.1f}h)'.format(total_rebuf[abr_cc] / 3600,
                                                   total_play[abr_cc] / 3600)

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

    fig.savefig(output, dpi=150, bbox_inches='tight')
    sys.stderr.write('Saved plot to {}\n'.format(output))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('yaml_settings')
    parser.add_argument('-o', '--output', required=True)
    parser.add_argument('--from', dest='time_start',
                        help='datetime in UTC conforming to RFC3339')
    parser.add_argument('--to', dest='time_end',
                        help='datetime in UTC conforming to RFC3339')
    args = parser.parse_args()
    output = args.output

    with open(args.yaml_settings, 'r') as fh:
        yaml_settings = yaml.safe_load(fh)

    # construct time clause after 'WHERE'
    time_clause = create_time_clause(args.time_start, args.time_end)

    # create an InfluxDB client and perform queries
    influx_client = connect_to_influxdb(yaml_settings)

    # query video_acked and client_buffer
    video_acked_query = 'SELECT * FROM video_acked'
    if time_clause is not None:
        video_acked_query += ' WHERE ' + time_clause
    video_acked_results = influx_client.query(video_acked_query)
    if not video_acked_results:
        sys.exit('Error: no results returned from query: ' + video_acked_query)

    client_buffer_query = 'SELECT * FROM client_buffer'
    if time_clause is not None:
        client_buffer_query += ' WHERE ' + time_clause
    client_buffer_results = influx_client.query(client_buffer_query)
    if not client_buffer_results:
        sys.exit('Error: no results returned from query: ' + client_buffer_query)

    # create a Postgres client and perform queries
    postgres_client = connect_to_postgres(yaml_settings)
    postgres_cursor = postgres_client.cursor()

    # collect ssim and rebuffer
    ssim = collect_ssim(video_acked_results, postgres_cursor)
    rebuffer, total_play, total_rebuf = collect_rebuffer(
        client_buffer_results, postgres_cursor)

    if not ssim or not rebuffer:
        sys.exit('Error: no data found in the queried range')

    # plot ssim vs rebuffer
    plot_ssim_rebuffer(ssim, rebuffer, total_play, total_rebuf, output,
                       args.time_start, args.time_end)

    postgres_cursor.close()


if __name__ == '__main__':
    main()
