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


def collect_ssim(video_acked_results, expt_id_cache, postgres_cursor):
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
    for abr_cc in x:
        avg_ssim_index = np.mean(x[abr_cc])
        avg_ssim_db = ssim_index_to_db(avg_ssim_index)
        x[abr_cc] = avg_ssim_db

    return x


def collect_buffer_data(client_buffer_results):
    d = {}  # indexed by session

    excluded_sessions = {}
    last_ts = {}
    last_buf = {}
    last_cum_rebuf = {}
    last_low_buf = {}

    for pt in client_buffer_results['client_buffer']:
        session = (pt['user'], int(pt['init_id']), int(pt['expt_id']))
        if session in excluded_sessions:
            continue

        if session not in d:
            d[session] = {}
            d[session]['min_play_time'] = None
            d[session]['max_play_time'] = None
            d[session]['min_cum_rebuf'] = None
            d[session]['max_cum_rebuf'] = None
        ds = d[session]  # short name

        if session not in last_ts:
            last_ts[session] = None
        if session not in last_buf:
            last_buf[session] = None
        if session not in last_cum_rebuf:
            last_cum_rebuf[session] = None
        if session not in last_low_buf:
            last_low_buf[session] = None

        ts = try_parsing_time(pt['time'])
        buf = float(pt['buffer'])
        cum_rebuf = float(pt['cum_rebuf'])

        # update d[session]
        if pt['event'] == 'startup':
            ds['min_play_time'] = ts
            ds['min_cum_rebuf'] = cum_rebuf

        if ds['min_play_time'] is None or ds['min_cum_rebuf'] is None:
            # wait until 'startup' is found
            continue

        if ds['max_play_time'] is None or ts > ds['max_play_time']:
            ds['max_play_time'] = ts

        if ds['max_cum_rebuf'] is None or cum_rebuf > ds['max_cum_rebuf']:
            ds['max_cum_rebuf'] = cum_rebuf

        # verify that time is basically successive in the same session
        if last_ts[session] is not None:
            diff = (ts - last_ts[session]).total_seconds()
            if diff > 60:  # ambiguous / suspicious session
                print('Ambiguous session', session)
                excluded_sessions[session] = True
                continue

        # identify outliers: exclude the sessions if there is a long rebuffer?
        if last_low_buf[session] is not None:
            diff = (ts - last_low_buf[session]).total_seconds()
            if diff > 30:
                print('Outlier session', session)
                excluded_sessions[session] = True
                continue

        # identify stalls caused by slow video decoding
        if last_buf[session] is not None and last_cum_rebuf[session] is not None:
            if (buf > 5 and last_buf[session] > 5 and
                cum_rebuf > last_cum_rebuf[session] + 0.25):
                print('Decoding stalls', session)
                excluded_sessions[session] = True
                continue

        # update last_XXX
        last_ts[session] = ts
        last_buf[session] = buf
        last_cum_rebuf[session] = cum_rebuf
        if buf > 0.1:
            last_low_buf[session] = None
        else:
            if last_low_buf[session] is None:
                last_low_buf[session] = ts

    ret = {}  # indexed by session

    # second pass to exclude short sessions
    for session in d:
        if session in excluded_sessions:
            continue

        ds = d[session]
        if ds['min_play_time'] is None or ds['min_cum_rebuf'] is None:
            # no 'startup' is found
            print('No startup found', session)
            continue

        sess_play = (ds['max_play_time'] - ds['min_play_time']).total_seconds()
        # exclude short sessions
        if sess_play < 5:
            continue

        sess_rebuf = ds['max_cum_rebuf'] - ds['min_cum_rebuf']
        if sess_rebuf > 300:
            print('Warning: bad session (rebuffer > 5min)', session)

        if session not in ret:
            ret[session] = {}

        ret[session]['play'] = sess_play
        ret[session]['rebuf'] = sess_rebuf
        ret[session]['startup'] = ds['min_cum_rebuf']

    print('Valid session count', len(ret))
    return ret


def calculate_rebuffer_by_abr_cc(d, expt_id_cache, postgres_cursor):
    x = {}  # indexed by (abr, cc)

    for session in d:
        expt_id = int(session[-1])
        expt_config = retrieve_expt_config(expt_id, expt_id_cache,
                                           postgres_cursor)
        abr_cc = (expt_config['abr'], expt_config['cc'])

        if abr_cc not in x:
            x[abr_cc] = {}
            x[abr_cc]['total_play'] = 0
            x[abr_cc]['total_rebuf'] = 0

        x[abr_cc]['total_play'] += d[session]['play']
        x[abr_cc]['total_rebuf'] += d[session]['rebuf']

    return x


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

    # cache of Postgres data: experiment 'id' -> json 'data' of the experiment
    expt_id_cache = {}

    # create a Postgres client and perform queries
    postgres_client = connect_to_postgres(yaml_settings)
    postgres_cursor = postgres_client.cursor()

    # collect ssim and rebuffer
    ssim = collect_ssim(video_acked_results, expt_id_cache, postgres_cursor)

    buffer_data = collect_buffer_data(client_buffer_results)
    rebuffer = calculate_rebuffer_by_abr_cc(buffer_data,
                                            expt_id_cache, postgres_cursor)

    if not ssim or not rebuffer:
        sys.exit('Error: no data found in the queried range')

    # plot ssim vs rebuffer
    plot_ssim_rebuffer(ssim, rebuffer, output, days)

    postgres_cursor.close()


if __name__ == '__main__':
    main()
