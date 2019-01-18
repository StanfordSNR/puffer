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
    ssim_index_to_db, retrieve_expt_config, create_time_clause)


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

        if pt['ssim_index'] is not None:
            ssim_index = float(pt['ssim_index'])
            x[abr_cc].append(ssim_index)

    # calculate average SSIM in dB and record number of chunks
    ssim = {}
    num_chunks = {}
    for abr_cc in x:
        num_chunks[abr_cc] = len(x[abr_cc])
        avg_ssim_index = np.mean(x[abr_cc])
        avg_ssim_db = ssim_index_to_db(avg_ssim_index)
        ssim[abr_cc] = avg_ssim_db

    return ssim, num_chunks


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

        session = (pt['user'], int(pt['init_id']),
                   pt['channel'], int(pt['expt_id']))
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


def plot_ssim_rebuffer(ssim, num_chunks, rebuffer, output,
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

        total_duration = (num_chunks[abr_cc] * 2.002) / 3600
        abr_cc_str += '\n({:.1f}h)'.format(total_duration)

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
    ssim, num_chunks = collect_ssim(video_acked_results, postgres_cursor)
    rebuffer = collect_rebuffer(client_buffer_results, postgres_cursor)

    if not ssim or not rebuffer:
        sys.exit('Error: no data found in the queried range')

    # plot ssim vs rebuffer
    plot_ssim_rebuffer(ssim, num_chunks, rebuffer, output,
                       args.time_start, args.time_end)

    postgres_cursor.close()


if __name__ == '__main__':
    main()
