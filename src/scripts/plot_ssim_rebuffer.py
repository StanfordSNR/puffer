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
    ssim_index_to_db, retrieve_expt_config, get_ssim_index,
    query_measurement)

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

    for pt in client_buffer_results['client_buffer']:
        session = (pt['user'], int(pt['init_id']), pt['expt_id'])
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
                sys.stderr.write('Ambiguous session: {}\n'.format(session))
                excluded_sessions[session] = True
                continue

        # identify stalls caused by slow video decoding
        if last_buf[session] is not None and last_cum_rebuf[session] is not None:
            if (buf > 5 and last_buf[session] > 5 and
                cum_rebuf > last_cum_rebuf[session] + 0.25):
                sys.stderr.write('Decoding stalls: {}\n'.format(session))
                excluded_sessions[session] = True
                continue

        # update last_XXX
        last_ts[session] = ts
        last_buf[session] = buf
        last_cum_rebuf[session] = cum_rebuf

    ret = {}  # indexed by session

    # second pass to exclude short sessions
    for session in d:
        if session in excluded_sessions:
            continue

        ds = d[session]
        if ds['min_play_time'] is None or ds['min_cum_rebuf'] is None:
            # no 'startup' is found
            sys.stderr.write('No startup found: {}\n'.format(session))
            continue

        sess_play = (ds['max_play_time'] - ds['min_play_time']).total_seconds()
        # exclude short sessions
        if sess_play < 10:
            continue

        sess_rebuf = ds['max_cum_rebuf'] - ds['min_cum_rebuf']
        if sess_rebuf > 300:
            sys.stderr.write('Warning: bad session (rebuffer > 5min): {}\n'
                             .format(session))

        if session not in ret:
            ret[session] = {}

        ret[session]['play'] = sess_play
        ret[session]['rebuf'] = sess_rebuf
        ret[session]['startup'] = ds['min_cum_rebuf']

    sys.stderr.write('Valid session count: {}\n'.format(len(ret)))
    return ret


def calculate_rebuffer_by_abr_cc(d, expt_id_cache, postgres_cursor):
    x = {}  # indexed by (abr, cc)

    for session in d:
        expt_id = int(session[-1])
        expt_config = retrieve_expt_config(expt_id, expt_id_cache,
                                           postgres_cursor)
        abr_cc = (expt_config['abr'], expt_config['cc'])

        if abr_cc not in x:
            x[abr_cc] = []

        x[abr_cc].append({'rebuf': d[session]['rebuf'],
                          'play': d[session]['play'],
                          'rate': d[session]['rebuf'] / d[session]['play'],
                         })

    return x


def filt_by_percentile(x, p):
    y = {}
    for abr_cc in x:
        seq_rate = np.array([t['rate'] for t in x[abr_cc]])
        th_rate = np.percentile(seq_rate, p)
        y[abr_cc] = [t for t in x[abr_cc] if t['rate'] <= th_rate]
        if len(y[abr_cc]) == 0:
            sys.exit('Error: not data for {} after percentile'.format(abr_cc))

    return y


def filt_unrebuf(x):
    y = {}
    for abr_cc in x:
        y[abr_cc] = [t for t in x[abr_cc] if t['rebuf'] > 1e-8]
        if len(y[abr_cc]) == 0:
            sys.exit('Error: not data for {} after percentile'.format(abr_cc))

    return y

def get_max_rate(x):
    y = {}
    for abr_cc in x:
        y[abr_cc] = max(x[abr_cc], key=lambda t: t['rate'])

    return y


def get_rate(x):
    y = {}
    for abr_cc in x:
        y[abr_cc] = [t['rate'] for t in x[abr_cc]]

    return y


def plot_ssim_rebuffer_dots(ssim, rebuffer, start_time, end_time, output):
    if end_time == None:
        end_time == 'now'
    title = ('Performance in [{}, {}) (UTC)'
             .format(start_time, end_time))

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

        rebuf_rate = rebuffer[abr_cc]['rate']

#        abr_cc_str += '\n({:.1f}m/{:.1f}h)'.format(
 #               rebuffer[abr_cc]['rebuf'] / 60,
  #              rebuffer[abr_cc]['play'] / 3600)

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


def plot_cdf(value_dict, x_min, x_max, time_start, time_end, xlabel, output):
    fig, ax = plt.subplots()

    num_bins = 100
    for term, values in value_dict.items():
        if not values:
            continue

        counts, bin_edges = np.histogram(values, bins=num_bins,
                                         range=(x_min, x_max))
        x = bin_edges
        y = np.cumsum(counts) / len(values)
        y = np.insert(y, 0, 0)  # prepend 0

        ax.plot(x, y, label=term)

    ax.set_xlim(x_min, x_max)
    ax.set_ylim(0, 1)
    ax.legend()
    ax.grid()

    title = ('Transmission Time Prediction Accuracy\n[{}, {}] (UTC)'
             .format(time_start, time_end))
    ax.set_title(title)
    ax.set_xlabel(xlabel)
    ax.set_ylabel('CDF')

    figname = output
    fig.savefig(figname, dpi=150, bbox_inches='tight')
    sys.stderr.write('Saved plot to {}\n'.format(figname))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('yaml_settings')
    parser.add_argument('--from', dest='time_start',
                        help='datetime in UTC conforming to RFC3339')
    parser.add_argument('--to', dest='time_end',
                        help='datetime in UTC conforming to RFC3339')
    parser.add_argument('-o', default='tmp')
    parser.add_argument('-d', '--days', type=int, default=1)
    parser.add_argument('--percentile', help='for percentile rebuffer')
    parser.add_argument('--plot-dots', action='store_true')
    parser.add_argument('--remove-unrebuf', action='store_true')
    parser.add_argument('--plot-cdf', action='store_true')
    args = parser.parse_args()

    with open(args.yaml_settings, 'r') as fh:
        yaml_settings = yaml.safe_load(fh)

    # create an InfluxDB client and perform queries
    influx_client = connect_to_influxdb(yaml_settings)

    # query video_acked and client_buffer
    video_acked_results = query_measurement(influx_client, 'video_acked',
         args.time_start, args.time_end)
    client_buffer_results = query_measurement(influx_client, 'client_buffer',
         args.time_start, args.time_end)

    # cache of Postgres data: experiment 'id' -> json 'data' of the experiment
    expt_id_cache = {}

    # create a Postgres client and perform queries
    postgres_client = connect_to_postgres(yaml_settings)
    postgres_cursor = postgres_client.cursor()

    # collect ssim and rebuffer
    ssim = collect_ssim(video_acked_results, expt_id_cache, postgres_cursor)

    buffer_data = collect_buffer_data(client_buffer_results)
    rebuffer_rate = calculate_rebuffer_by_abr_cc(buffer_data,
                                                expt_id_cache, postgres_cursor)

    if args.remove_unrebuf:
        rebuffer_rate = filt_unrebuf(rebuffer_rate)

    if args.percentile:
        p = float(args.percentile)
        rebuffer_rate = filt_by_percentile(rebuffer_rate, p)

    if not ssim or not rebuffer_rate:
        sys.exit('Error: no data found in the queried range')

    if args.plot_dots:
        # plot ssim vs rebuffer
        #if args.percentile:
        #   output += '_pt_' + args.percentile
        rebuffer = get_max_rate(rebuffer_rate)
        plot_ssim_rebuffer_dots(ssim, rebuffer, args.time_start, args.time_end,
                                args.o + '_dots')

    if args.plot_cdf:
        rebuffer_rate = get_rate(rebuffer_rate)
        plot_cdf(rebuffer_rate, 0, 0.0005, args.time_start,
                                args.time_end, 'rebuffer_rate',
                                args.o + '_cdf')

    postgres_cursor.close()


if __name__ == '__main__':
    main()
