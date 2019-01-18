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
        dsv['size'] = float(pt['size']) / PKT_BYTES  # bytes -> packets
        # byte/second -> packet/second
        dsv['delivery_rate'] = float(pt['delivery_rate']) / PKT_BYTES
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
    real_tputs = []
    err_list = []
    for session in d:
        ds = d[session]

        first_ts = True
        for next_ts in ds:
            if 'trans_time' not in ds[next_ts]:
                continue

            real_tput = ds[next_ts]['trans_time'] / ds[next_ts]['size']

            if estimator == "tcp_info":
                est_tput = ds[next_ts]['delivery_rate']
            elif esimator == "last_tput":
                if first_ts: # first chunk
                    est_tput = 0
                else:
                    est_tput = real_tputs[-1]
            elif estimator == "robust_mpc" pr estimator == "mpc": # Harmonic mean of last 5 tputs
                past_bandwidths = past_tputs[:-5]
                while past_bandwidths[0] == 0.0:
                    past_bandwidths = past_bandwidths[1:]
                bandwidth_sum = 0

                for past_val in past_bandwidths:
                    bandwidth_sum += (1/float(past_val))
                harmonic_bandwidth = 1.0/(bandwidth_sum/len(past_bandwidths))
                max_error = 0
                error_pos = -5
                if ( len(past_errors) < 5 ):
                    error_pos = -len(past_errors)
                max_error = float(max(past_errors[error_pos:]))
                robust_mpc_bandwidth = harmonic_bandwidth/(1+max_error)
                if estimator == "mpc":
                    est_tput = harmonic_bandwidth
                else:
                    est_tput = robust_mpc_bandwidth
            first_ts = False


            real_tputs.append[real_tput]
            err_list.append(abs(real_tput - est_tput))

    return err_list


def plot_accuracy_cdf(err_lists):
    #fig = plt.figure()
    #ax = fig.add_subplot(111)

    for errors in err_lists:
        values, base = np.histogram(errors, bins=
       errors /= errors.sum() #Normalize to PDF

        plt.hist(errors, normed=True, cumulative=True, label='CDF',
                 histtype='step', alpha=0.8, color='k')
    plt.xlabel('Throughput estimate error')
    plt.ylabel('CDF')
    plt.show()

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('yaml_settings')
    parser.add_argument('--from', dest='time_start',
                        help='datetime in UTC conforming to RFC3339')
    parser.add_argument('--to', dest='time_end',
                        help='datetime in UTC conforming to RFC3339')
    parser.add_argument('--cc', help='filter input data by congestion control')
    args = parser.parse_args()

    # query InfluxDB and retrieve raw data
    raw_data = prepare_raw_data(args.yaml_settings,
                                args.time_start, args.time_end, args.cc)

    # collect input and output data from raw data
    #TODO collect all 3 at once
    err_lists = []
    tcp_err_list = calc_throughput_err(raw_data, "tcp_info")
    err_lists.append((tcp_err_list, "tcp_info"))
    #mpc_err_list = calc_throughput_err(raw_data, "mpc")
    #last_tput_err_list = calc_throughput_err(raw_data, "last_tput")
    #err_lists.append((mpc_err_list, "mpc"))
    #err_lists.append((last_tput_err_list, "last_tput"))

    plot_accuracy_cdf(err_lists)



if __name__ == '__main__':
    main()
