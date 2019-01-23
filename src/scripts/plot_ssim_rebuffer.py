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
    ssim_index_to_db, retrieve_expt_config, get_ssim_index)


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
    last_ts = {}
    last_cum_rebuf = {}
    outlier_time = {}
    excluded_sessions = {}
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

        if session not in last_ts:
            last_ts[session] = None
        if session not in last_cum_rebuf:
            last_cum_rebuf[session] = None
        if session not in outlier_time:
            outlier_time[session] = None

        if session not in x[abr_cc]:
            if session in excluded_sessions:
                # ignore sessions that were intentionally removed from x[abr_cc]
                continue

            x[abr_cc][session] = {}
            x[abr_cc][session]['min_play_time'] = None
            x[abr_cc][session]['max_play_time'] = None
            x[abr_cc][session]['min_cum_rebuf'] = None
            x[abr_cc][session]['max_cum_rebuf'] = None

        y = x[abr_cc][session]  # short name

        ts = try_parsing_time(pt['time'])
        buf = float(pt['buffer'])
        cum_rebuf = float(pt['cum_rebuf'])

        # verify that time is basically continuous in the same session
        if last_ts[session] is not None:
            diff = (ts - last_ts[session]).total_seconds()
            if diff > 60:  # a new but different session should be ignored
                continue
        last_ts[session] = ts

        # identify outliers: exclude the session if there is one-minute long
        # duration when buffer is lower than 1 second
        if buf > 1:
            outlier_time[session] = None
        else:
            if outlier_time[session] is None:
                outlier_time[session] = ts
            else:
                diff = (ts - outlier_time[session]).total_seconds()
                if diff > 30:
                    print('Outlier session', abr_cc, session)
                    del x[abr_cc][session]
                    excluded_sessions[session] = True
                    continue

        # identify stalls caused by slow video decoding
        if last_cum_rebuf[session] is not None:
            if buf > 5 and cum_rebuf > last_cum_rebuf[session] + 0.25:
                # should not have stalls
                print('Decoding stalls', abr_cc, session)
                del x[abr_cc][session]
                excluded_sessions[session] = True
                continue

        last_cum_rebuf[session] = cum_rebuf

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

            # exclude short sessions
            if sess_play < 5:
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


def plot_ssim_rebuffer(ssim, rebuffer, total_play, total_rebuf, output, days):
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

        abr_cc_str += '\n({:.1f}m/{:.1f}h)'.format(total_rebuf[abr_cc] / 60,
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
    rebuffer, total_play, total_rebuf = collect_rebuffer(
        client_buffer_results, postgres_cursor)

    if not ssim or not rebuffer:
        sys.exit('Error: no data found in the queried range')

    # plot ssim vs rebuffer
    plot_ssim_rebuffer(ssim, rebuffer, total_play, total_rebuf, output, days)

    postgres_cursor.close()


if __name__ == '__main__':
    main()
