#!/usr/bin/env python3

import sys
import json
import argparse
import yaml
from os import path
import numpy as np
from multiprocessing import Process

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

from helpers import (
    connect_to_influxdb, connect_to_postgres, try_parsing_time,
    make_sure_path_exists, retrieve_expt_config)


VIDEO_DURATION = 180180
PKT_BYTES = 1500
MILLION = 1000000

# cache of Postgres data: experiment 'id' -> json 'data' of the experiment
expt_id_cache = {}


def create_time_clause(time_start, time_end):
    time_clause = None

    if time_start is not None:
        time_clause = "time >= '{}'".format(time_start)
    if time_end is not None:
        if time_clause is None:
            time_clause = "time <= '{}'".format(time_end)
        else:
            time_clause += " AND time <= '{}'".format(time_end)

    return time_clause


def get_ssim_index(pt):
    if 'ssim_index' in pt and pt['ssim_index'] is not None:
        return float(pt['ssim_index'])

    if 'ssim' in pt and pt['ssim'] is not None:
        return ssim_db_to_index(float(pt['ssim']))

    return None


def calculate_trans_times(video_sent_results, video_acked_results,
                          cc, postgres_cursor):
    d = {}
    last_video_ts = {}

    for pt in video_sent_results['video_sent']:
        expt_id = int(pt['expt_id'])
        session = (pt['user'], int(pt['init_id']),
                   pt['channel'], expt_id)

        # filter data points by congestion control
        expt_config = retrieve_expt_config(expt_id, expt_id_cache,
                                           postgres_cursor)
        if cc is not None and expt_config['cc'] != cc:
            continue

        if session not in d:
            d[session] = {}
            last_video_ts[session] = None

        video_ts = int(pt['video_ts'])

        if last_video_ts[session] is not None:
            if video_ts != last_video_ts[session] + VIDEO_DURATION:
                sys.stderr.write('Warning in session {}: video_ts={}\n'
                                 .format(session, video_ts))
                continue

        last_video_ts[session] = video_ts

        d[session][video_ts] = {}
        dsv = d[session][video_ts]  # short name

        dsv['sent_ts'] = try_parsing_time(pt['time'])
        dsv['size'] = float(pt['size']) * 8 # bits
        dsv['delivery_rate'] = float(pt['delivery_rate']) * 8 # bps
        dsv['cwnd'] = float(pt['cwnd'])
        dsv['in_flight'] = float(pt['in_flight'])
        dsv['min_rtt'] = float(pt['min_rtt']) / MILLION  # us -> s
        dsv['rtt'] = float(pt['rtt']) / MILLION  # us -> s
        # dsv['ssim_index'] = get_ssim_index(pt)

    for pt in video_acked_results['video_acked']:
        expt_id = int(pt['expt_id'])
        session = (pt['user'], int(pt['init_id']),
                   pt['channel'], expt_id)

        # filter data points by congestion control
        expt_config = retrieve_expt_config(expt_id, expt_id_cache,
                                           postgres_cursor)
        if cc is not None and expt_config['cc'] != cc:
            continue

        if session not in d:
            sys.stderr.write('Warning: ignored session {}\n'.format(session))
            continue

        video_ts = int(pt['video_ts'])
        if video_ts not in d[session]:
            sys.stderr.write('Warning: ignored acked video_ts {} in the '
                             'session {}\n'.format(video_ts, session))
            continue

        dsv = d[session][video_ts]  # short name

        # calculate transmission time
        sent_ts = dsv['sent_ts']
        acked_ts = try_parsing_time(pt['time'])
        dsv['acked_ts'] = acked_ts
        dsv['trans_time'] = (acked_ts - sent_ts).total_seconds()

    return d


def prepare_raw_data(yaml_settings_path, time_start, time_end, cc):
    with open(yaml_settings_path, 'r') as fh:
        yaml_settings = yaml.safe_load(fh)

    # construct time clause after 'WHERE'
    time_clause = create_time_clause(time_start, time_end)

    # create a client connected to InfluxDB
    influx_client = connect_to_influxdb(yaml_settings)

    # perform queries in InfluxDB
    video_sent_query = 'SELECT * FROM video_sent'
    if time_clause is not None:
        video_sent_query += ' WHERE ' + time_clause
    video_sent_results = influx_client.query(video_sent_query)
    if not video_sent_results:
        sys.exit('Error: no results returned from query: ' + video_sent_query)

    video_acked_query = 'SELECT * FROM video_acked'
    if time_clause is not None:
        video_acked_query += ' WHERE ' + time_clause
    video_acked_results = influx_client.query(video_acked_query)
    if not video_acked_results:
        sys.exit('Error: no results returned from query: ' + video_acked_query)

    # create a client connected to Postgres
    postgres_client = connect_to_postgres(yaml_settings)
    postgres_cursor = postgres_client.cursor()

    # calculate chunk transmission times
    ret = calculate_trans_times(video_sent_results, video_acked_results,
                                cc, postgres_cursor)

    postgres_cursor.close()
    return ret


def calc_throughput_err(d, estimator):
    err_list_ms = []
    err_list_percent = []
    max_tput = 0
    for session in d:
        ds = d[session]

        real_tputs = []
        for next_ts in ds:
            if 'trans_time' not in ds[next_ts]:
                continue

            #print(ds[next_ts]['trans_time'])
            real_tput = ds[next_ts]['size'] / ds[next_ts]['trans_time']  # bps
            if real_tput > max_tput:
                max_tput = real_tput

            if estimator == "tcp_info":
                est_tput = ds[next_ts]['delivery_rate']
            elif estimator == "last_tput":
                if len(real_tputs) == 0: # first chunk
                    est_tput = -1 # Will be ignored
                else:
                    est_tput = real_tputs[-1]
            elif estimator == "mpc": # Harmonic mean of last 5 tputs
                if len(real_tputs) < 5: # first 5 chunks
                    est_tput = -1
                else:
                    past_bandwidths = real_tputs[-5:]

                    bandwidth_sum = 0

                    for past_val in past_bandwidths:
                        bandwidth_sum += (1/float(past_val))
                    if bandwidth_sum == 0:
                        print("bwidth_sum = 0, past_bandwidths:" + str(past_bandwidths) + ", real_tputs: " + str(real_tputs))
                    harmonic_bandwidth = 1.0/(bandwidth_sum/len(past_bandwidths))
                    est_tput = harmonic_bandwidth


            real_tputs.append(real_tput)
            #print("real_tput: " + str(real_tput) + ", est_tput: " + str(est_tput))
            #err_list.append(abs(real_tput - est_tput) / 1000000) # bps --> Mbps
            if(est_tput != -1): # Ignore first 5 chunks
                est_trans_time = ds[next_ts]['size'] / est_tput
                err_list_ms.append(abs(ds[next_ts]['trans_time'] - est_trans_time) * 1000) # s --> ms
                err_list_percent.append(abs(ds[next_ts]['trans_time'] - est_trans_time)/ds[next_ts]['trans_time'] * 100)

    return (err_list_ms, err_list_percent, estimator)


def plot_accuracy_cdf(err_lists, time_start, time_end):
    fig, ax = plt.subplots()

    # first plot with x axis in ms
    for errors in err_lists:
        #print(errors[0])
        ax.hist(errors[0], bins=100000, normed=1, cumulative=True, label=errors[2],
                histtype='step')
    ax.legend(loc='right')
    ax.grid(True)
    ax.set_xlabel('Transmission time estimate error (ms)')
    ax.set_ylabel('CDF')
    title = ('Throughput Estimator Accuracy from [{}, {}) (UTC)'.format(time_start, time_end))
    xmin, xmax = ax.get_xlim()
    xmax = 4000
    ax.set_xlim(0, xmax)
    ax.set_title(title)
    fig.savefig("throughput_err.png", dpi=900, bbox_inches='tight', pad_inches=0.2)
    sys.stderr.write('Saved plot on ms scale to {}\n'.format("throughput_err.png"))

    fig, ax = plt.subplots()
    # next plot with x axis in percent
    for errors in err_lists:
        #print(errors[0])
        ax.hist(errors[1], bins=100000, normed=1, cumulative=True, label=errors[2],
                histtype='step')
    ax.legend(loc='right')
    ax.grid(True)
    ax.set_xlabel('Transmission time estimate error (%)')
    ax.set_ylabel('CDF')
    title = ('Throughput Estimator Accuracy from [{}, {}) (UTC)'.format(time_start, time_end))
    xmin, xmax = ax.get_xlim()
    xmax = 125
    ax.set_xlim(0, xmax)
    ax.set_title(title)
    fig.savefig("throughput_err_percent.png", dpi=900, bbox_inches='tight', pad_inches=0.2)
    sys.stderr.write('Saved plot on % scale to {}\n'.format("throughput_err_percent.png"))


def plot_session_duration_and_throughput(d, time_start, time_end):
    session_durations = []
    tputs = []
    for session in d:
        ds = d[session]

        session_durations.append(len(ds))
        tput_sum = 0
        tput_count = 0
        for next_ts in ds:
            tput_sum += (ds[next_ts]['size'] / ds[next_ts]['trans_time'] / 1000000) # Mbps
            tput_count += 1
        tputs.append(tput_sum/tput_count) # Average tput for session

    fig, ax = plt.subplots()
    ax.hist(session_durations, bins=100000, normed=1, cumulative=True,
            histtype='step')
    ax.grid(True)
    ax.set_xlabel('Session duration (s)')
    ax.set_ylabel('CDF')
    title = ('Session Duration from [{}, {}) (UTC)'.format(time_start, time_end))
    xmin, xmax = ax.get_xlim()
    #xmax = 4000
    ax.set_xlim(0, xmax)
    ax.set_title(title)
    fig.savefig("session_duration.png", dpi=900, bbox_inches='tight', pad_inches=0.2)
    sys.stderr.write('Saved session duration plot to {}\n'.format("session_duration.png"))

    fig, ax = plt.subplots()
    ax.hist(tputs, bins=100000, normed=1, cumulative=True,
            histtype='step')
    ax.grid(True)
    ax.set_xlabel('Session throughput (Mbps)')
    ax.set_ylabel('CDF')
    title = ('Session Throughputs from [{}, {}) (UTC)'.format(time_start, time_end))
    xmin, xmax = ax.get_xlim()
    #xmax = 4000
    ax.set_xlim(0, xmax)
    ax.set_title(title)
    fig.savefig("session_throughputs.png", dpi=900, bbox_inches='tight', pad_inches=0.2)
    sys.stderr.write('Saved session throughputs plot to {}\n'.format("session_throughputs.png"))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('yaml_settings')
    parser.add_argument('--from', dest='time_start',
                        help='datetime in UTC conforming to RFC3339')
    parser.add_argument('--to', dest='time_end',
                        help='datetime in UTC conforming to RFC3339')
    parser.add_argument('--cc', help='filter input data by congestion control')
    parser.add_argument('-s', '--session_info', help='plot session info', action='store_true')
    parser.add_argument('-t', '--tput_estimates', help='plot throughput estimate accuracies', action='store_true')
    args = parser.parse_args()

    if not args.tput_estimates and not args.session_info:
        sys.exit("Please pass either -s, -t, or both to execute the portion of this script you would like to run")

    # query InfluxDB and retrieve raw data
    raw_data = prepare_raw_data(args.yaml_settings,
                                args.time_start, args.time_end, args.cc)

    # collect input and output data from raw data
    #TODO collect all 3 at once
    if args.tput_estimates:
        err_lists = []
        tcp_err_list = calc_throughput_err(raw_data, "tcp_info")
        mpc_err_list = calc_throughput_err(raw_data, "mpc")
        last_tput_err_list = calc_throughput_err(raw_data, "last_tput")
        err_lists.append(tcp_err_list)
        err_lists.append(mpc_err_list)
        err_lists.append(last_tput_err_list)

        plot_accuracy_cdf(err_lists, args.time_start, args.time_end)
    if args.session_info:
        plot_session_duration_and_throughput(raw_data, args.time_start, args.time_end)



if __name__ == '__main__':
    main()
