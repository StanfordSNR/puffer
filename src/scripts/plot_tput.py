#!/usr/bin/env python3

import sys
import yaml
import pickle
import json
import argparse
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

from helpers import (
    connect_to_influxdb, connect_to_postgres, query_measurement,
    retrieve_expt_config, filter_video_data_by_cc)
from collect_data import (
    video_data_by_session, VIDEO_DURATION, video_size_by_channel_timestamp)
import ttp

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('yaml_settings')
    parser.add_argument('--from', dest='time_start',
                        help='datetime in UTC conforming to RFC3339')
    parser.add_argument('--to', dest='time_end',
                        help='datetime in UTC conforming to RFC3339')
    parser.add_argument('--cc', help='filter input data by congestion control')
    parser.add_argument('--plot-epoch', default=200)
    args = parser.parse_args()

    yaml_settings_path = args.yaml_settings
    with open(yaml_settings_path, 'r') as fh:
        yaml_settings = yaml.safe_load(fh)

    # create an InfluxDB client and perform queries
    influx_client = connect_to_influxdb(yaml_settings)

    # query data from video_sent and video_acked
    video_sent_results = query_measurement(influx_client, 'video_sent',
                                           args.time_start, args.time_end)
    video_acked_results = query_measurement(influx_client, 'video_acked',
                                            args.time_start, args.time_end)
    video_data = video_data_by_session(video_sent_results, video_acked_results)

    # modify video_data in place by deleting sessions not with args.cc
    filter_video_data_by_cc(video_data, yaml_settings, args.cc)

    fit_sess = None
    min_max = 1000
    min_mean = 0
    fit_mean = 2.4
    epoch_time = 6
    mbps_unit = 125000

    for sess_id, sess in video_data.items():
        if len(sess) < 1300:
            continue

        print("one")

        mean = sum([t['size'] / t['trans_time'] / mbps_unit \
                    for t in sess.values()]) \
                    / len(sess)
        max_t = min(t['size'] / t['trans_time'] for t in sess.values())

        min_max = max(max_t, min_max)

        if fit_sess == None or \
           (abs(min_mean - fit_mean) > abs(mean - fit_mean)):
            min_sess_id = sess_id
            fit_sess = sess
            min_mean = mean

    print(min_mean, min_sess_id)

    raw = {}
    for t in fit_sess.values():
        epoch = int(t['ts'] / epoch_time)
        if epoch not in raw:
            raw[epoch] = []
        raw[epoch].append(float(t['size']) / t['trans_time'] / mbps_unit)

    x = []
    y = []
    for ts, tputs in raw.items():
        y.append(sum(tputs) / len(tputs))
        x.append(ts)

    x_min = min(x)
    x = [t - x_min for t in x]

    plot_epoch = int(args.plot_epoch)

    plt.xlim(0, plot_epoch)
    plt.plot(x[:plot_epoch], y[:plot_epoch])
    plt.ylabel('Thoughput (Mbps)')
    plt.xlabel('Epoch (6 seconds each)')

    figname = 'tput_' + str(plot_epoch) + '_' + str(epoch_time) + '_' \
              + str(fit_mean) + '-' + args.time_start + '-' \
              + args.time_end + '.svg'
    plt.savefig(figname)

if __name__ == '__main__':
    main()
