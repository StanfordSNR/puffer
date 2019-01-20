#!/usr/bin/env python3

import sys
import json
import argparse
from os import path
import numpy as np
from multiprocessing import Process
import matplotlib
import matplotlib.pyplot as plt
from helpers import (
    connect_to_influxdb, connect_to_postgres, try_parsing_time,
    make_sure_path_exists, retrieve_expt_config, prepare_raw_data)

matplotlib.use('Agg')

MAX_TRANS_TIME_ESTIMATE = 10  # seconds


def calc_throughput_err(d, estimator):
    err_list_ms = []
    err_list_percent = []
    max_tput = 0
    for session in d:
        ds = d[session]

        real_tputs = []
        for next_ts in sorted(ds.keys()):  # Need to iterate in order!
            if 'trans_time' not in ds[next_ts]:
                continue

            real_tput = ds[next_ts]['size'] / ds[next_ts]['trans_time']  # bps
            if real_tput > max_tput:
                max_tput = real_tput

            if estimator == 'tcp_info':
                est_tput = ds[next_ts]['delivery_rate']
            elif estimator == 'last_tput':
                if len(real_tputs) == 0:  # first chunk
                    est_tput = -1  # Will be ignored
                else:
                    est_tput = real_tputs[-1]
            elif estimator == 'mpc':  # Harmonic mean of last 5 tputs
                if len(real_tputs) < 5:  # first 5 chunks
                    est_tput = -1
                else:
                    past_bandwidths = real_tputs[-5:]

                    bandwidth_sum = 0

                    for past_val in past_bandwidths:
                        bandwidth_sum += (1/float(past_val))
                    harmonic_bandwidth = 1.0/(bandwidth_sum/len(past_bandwidths))
                    est_tput = harmonic_bandwidth

            real_tputs.append(real_tput)
            if(est_tput != -1):  # Ignore first 5 chunks
                if est_tput > 0:
                    est_trans_time = ds[next_ts]['size'] / est_tput
                else:
                    est_trans_time = MAX_TRANS_TIME_ESTIMATE
                err_list_ms.append(abs(ds[next_ts]['trans_time'] -
                                   est_trans_time) * 1000)  # s --> ms
                err_list_percent.append(abs(ds[next_ts]['trans_time'] -
                                        est_trans_time)/ds[next_ts]['trans_time'] * 100)

    return (err_list_ms, err_list_percent, estimator)


def plot_accuracy_cdf(err_lists, time_start, time_end):
    fig, ax = plt.subplots()

    # first plot with x axis in ms
    for errors in err_lists:
        ax.hist(errors[0], bins=100000, normed=1, cumulative=True,
                label=errors[2], histtype='step')
    ax.legend(loc='right')
    ax.grid(True)
    ax.set_xlabel('Transmission time estimate error (ms)')
    ax.set_ylabel('CDF')
    title = ('Throughput Estimator Accuracy from [{}, {}) \
              (UTC)'.format(time_start, time_end))
    xmin, xmax = ax.get_xlim()
    xmax = 4000
    ax.set_xlim(0, xmax)
    ax.set_title(title)
    fig.savefig('throughput_err.png', dpi=900, bbox_inches='tight',
                pad_inches=0.2)
    sys.stderr.write('Saved plot on ms scale to {}\n'
                     .format('throughput_err.png'))

    fig, ax = plt.subplots()
    # next plot with x axis in percent
    for errors in err_lists:
        ax.hist(errors[1], bins=100000, normed=1, cumulative=True,
                label=errors[2], histtype='step')
    ax.legend(loc='right')
    ax.grid(True)
    ax.set_xlabel('Transmission time estimate error (%)')
    ax.set_ylabel('CDF')
    title = ('Throughput Estimator Accuracy from [{}, {}) \
              (UTC)'.format(time_start, time_end))
    xmin, xmax = ax.get_xlim()
    xmax = 125
    ax.set_xlim(0, xmax)
    ax.set_title(title)
    fig.savefig('throughput_err_percent.png', dpi=900, bbox_inches='tight',
                pad_inches=0.2)
    sys.stderr.write('Saved plot on % scale to {}\n'
                     .format('throughput_err_percent.png'))


def plot_session_duration_and_throughput(d, time_start, time_end):
    session_durations = []
    tputs = []
    total_rebuffer = []
    rebuffer_percent = []
    for session in d:
        ds = d[session]
        if len(ds) < 5:
            # Dont count sessions that dont deliver 10 seconds of video
            continue
        session_durations.append(len(ds) * 2.002 / 60)  # minutes
        tput_sum = 0
        tput_count = 0
        first_ts = True
        second_ts = True
        for next_ts in sorted(ds.keys()):  # Need to iterate in order!
            if 'trans_time' not in ds[next_ts]:
                continue
            if first_ts:
                first_ts = False
            elif second_ts:  # TODO: Better method for detecting startup delay
                startup_delay = ds[next_ts]['cum_rebuffer']
                second_ts = False
            tput_sum += (ds[next_ts]['size'] / ds[next_ts]['trans_time'] /
                         1000000)  # Mbps
            tput_count += 1
        if tput_count == 0:
            continue
        if not first_ts:
            # Rebuffer time = cum rebuffer of last chunk - startup delay
            total_rebuffer.append(ds[sorted(ds.keys())[-1]]['cum_rebuffer'] -
                                  startup_delay)
            rebuffer_percent.append(total_rebuffer[-1] / (session_durations[-1] + total_rebuffer[-1]) * 100)
        tputs.append(tput_sum/tput_count)  # Average tput for session

    fig, ax = plt.subplots()
    ax.hist(session_durations, bins=100000, normed=1, cumulative=True,
            histtype='step')
    ax.grid(True)
    ax.set_xlabel('Session duration (minutes)')
    ax.set_ylabel('CDF')
    title = ('Session Duration from [{}, {}) \
              (UTC)'.format(time_start, time_end))
    xmin, xmax = ax.get_xlim()
    xmax = 50
    ax.set_xlim(0, xmax)
    ax.set_title(title)
    fig.savefig('session_duration.png', dpi=900, bbox_inches='tight',
                pad_inches=0.2)
    sys.stderr.write('Saved session duration plot to \
                      {}\n'.format('session_duration.png'))

    fig, ax = plt.subplots()
    ax.hist(tputs, bins=100000, normed=1, cumulative=True,
            histtype='step')
    ax.grid(True)
    ax.set_xlabel('Session throughput (Mbps)')
    ax.set_ylabel('CDF')
    title = ('Session Throughputs from [{}, {}) \
              (UTC)'.format(time_start, time_end))
    xmin, xmax = ax.get_xlim()
    xmax = 100
    ax.set_xlim(0, xmax)
    ax.set_title(title)
    fig.savefig('session_throughputs.png', dpi=900, bbox_inches='tight',
                pad_inches=0.2)
    sys.stderr.write('Saved session throughputs plot to \
                      {}\n'.format('session_throughputs.png'))

    fig, ax = plt.subplots()
    ax.hist(rebuffer_percent, bins=100000, normed=1, cumulative=True,
            histtype='step')
    ax.grid(True)
    ax.set_xlabel('% Rebuffer (excluding startup) - all ABR')
    ax.set_ylabel('CDF')
    title = ('Rebuffer % (all ABR) from [{}, {}) \
             (UTC)'.format(time_start, time_end))
    xmin, xmax = ax.get_xlim()
    # xmax = 100
    ax.set_xlim(0, xmax)
    ax.set_title(title)
    fig.savefig('rebuffer_percent.png', dpi=900, bbox_inches='tight',
                pad_inches=0.2)
    sys.stderr.write('Saved rebuffer percent plot to \
                      {}\n'.format('rebuffer_percent.png'))

    print('Percentage of all sessions with a rebuffer: ' +
          str(np.count_nonzero(total_rebuffer) / len(session_durations)))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('yaml_settings')
    parser.add_argument('--from', dest='time_start',
                        help='datetime in UTC conforming to RFC3339')
    parser.add_argument('--to', dest='time_end',
                        help='datetime in UTC conforming to RFC3339')
    parser.add_argument('--cc', help='filter input data by congestion control')
    parser.add_argument('-s', '--session-info', help='plot session info',
                        action='store_true')
    parser.add_argument('-t', '--tput-estimates',
                        help='plot throughput estimate accuracies',
                        action='store_true')
    args = parser.parse_args()

    if not args.tput_estimates and not args.session_info:
        sys.exit('Please pass either -s, -t, or both to execute the portion '
                 'of this script you would like to run')

    # query InfluxDB and retrieve raw data
    raw_data = prepare_raw_data(args.yaml_settings,
                                args.time_start, args.time_end, args.cc)

    # collect input and output data from raw data
    # TODO faster to collect all 3 at once
    if args.tput_estimates:
        err_lists = []
        tcp_err_list = calc_throughput_err(raw_data, 'tcp_info')
        mpc_err_list = calc_throughput_err(raw_data, 'mpc')
        last_tput_err_list = calc_throughput_err(raw_data, 'last_tput')
        err_lists.append(tcp_err_list)
        err_lists.append(mpc_err_list)
        err_lists.append(last_tput_err_list)

        plot_accuracy_cdf(err_lists, args.time_start, args.time_end)
    if args.session_info:
        plot_session_duration_and_throughput(raw_data, args.time_start,
                                             args.time_end)


if __name__ == '__main__':
    main()
