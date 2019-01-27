#!/usr/bin/env python3

import os
import sys
import argparse
import yaml
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

from helpers import (
    connect_to_postgres, connect_to_influxdb, ssim_index_to_db,
    retrieve_expt_config, get_ssim_index, get_abr_cc, query_measurement)
from collect_data import video_data_by_session, VIDEO_DURATION


def get_video_data(influx_client, args):
    video_sent_results = query_measurement(influx_client, 'video_sent',
                                           args.time_start, args.time_end)
    video_acked_results = query_measurement(influx_client, 'video_acked',
                                            args.time_start, args.time_end)

    video_data = video_data_by_session(video_sent_results, video_acked_results)

    return video_data


def collect_ssim_diff(d, expt_id_cache, postgres_cursor, args):
    # index by abr, they by cc
    ssim_diff = {}

    for session in d:
        expt_id = session[-1]
        expt_config = retrieve_expt_config(expt_id, expt_id_cache,
                                           postgres_cursor)
        abr_cc = get_abr_cc(expt_config)

        if abr_cc not in ssim_diff:
            ssim_diff[abr_cc] = []

        for video_ts in d[session]:
            dsv = d[session][video_ts]

            # append SSIM
            curr_ssim_index = dsv['ssim_index']
            if curr_ssim_index == 1:
                continue

            curr_ssim_db = ssim_index_to_db(curr_ssim_index)

            # append SSIM variation
            prev_ts = video_ts - VIDEO_DURATION
            if prev_ts not in d[session]:
                continue

            prev_ssim_index = d[session][prev_ts]['ssim_index']
            if prev_ssim_index == 1:
                continue

            prev_ssim_db = ssim_index_to_db(prev_ssim_index)
            abs_diff = abs(curr_ssim_db - prev_ssim_db)
            ssim_diff[abr_cc].append(abs_diff)

    for abr_cc in ssim_diff:
        ssim_diff[abr_cc] = np.mean(ssim_diff[abr_cc])

    return ssim_diff


def collect_ssim_mean(video_acked_results, expt_id_cache, postgres_cursor):
    # process InfluxDB data
    x = {}
    for pt in video_acked_results['video_acked']:
        expt_id = pt['expt_id']
        expt_config = retrieve_expt_config(expt_id, expt_id_cache,
                                           postgres_cursor)

        abr_cc = get_abr_cc(expt_config)
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


def collect_ssim(influx_client, expt_id_cache, postgres_cursor, args):
    video_sent_results = query_measurement(influx_client, 'video_sent',
                                           args.time_start, args.time_end)
    video_acked_results = query_measurement(influx_client, 'video_acked',
                                            args.time_start, args.time_end)

    video_data = video_data_by_session(video_sent_results, video_acked_results)

    ssim_mean = collect_ssim_mean(video_acked_results,
                                  expt_id_cache, postgres_cursor)

    ssim_diff = collect_ssim_diff(video_data,
                                  expt_id_cache, postgres_cursor, args)

    return ssim_mean, ssim_diff


def plot_ssim(ssim_mean, ssim_diff, args):
    fig, ax = plt.subplots()
    title = '[{}, {}] (UTC)'.format(args.time_start, args.time_end)
    ax.set_title(title)
    ax.set_xlabel('Average SSIM variation (dB)')
    ax.set_ylabel('Average SSIM (dB)')
    ax.grid()

    for abr_cc in ssim_mean:
        abr_cc_str = '{}+{}'.format(*abr_cc)
        if abr_cc not in ssim_diff:
            sys.exit('Error: {} does not exist in ssim_diff'
                     .format(abr_cc))

        x = ssim_diff[abr_cc]
        y = ssim_mean[abr_cc]
        ax.scatter(x, y)
        ax.annotate(abr_cc_str, (x, y))

    xmin, xmax = ax.get_xlim()
    xmin = max(xmin, 0)
    ax.set_xlim(xmin, xmax)
    ax.invert_xaxis()

    fig.savefig(args.output, dpi=150, bbox_inches='tight')
    sys.stderr.write('Saved plot to {}\n'.format(args.output))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('yaml_settings')
    parser.add_argument('--from', dest='time_start',
                        help='datetime in UTC conforming to RFC3339')
    parser.add_argument('--to', dest='time_end',
                        help='datetime in UTC conforming to RFC3339')
    parser.add_argument('-o', '--output', required=True)
    args = parser.parse_args()

    with open(args.yaml_settings, 'r') as fh:
        yaml_settings = yaml.safe_load(fh)

    # create an InfluxDB client and perform queries
    influx_client = connect_to_influxdb(yaml_settings)

    # cache of Postgres data: experiment 'id' -> json 'data' of the experiment
    expt_id_cache = {}

    # create a Postgres client and perform queries
    postgres_client = connect_to_postgres(yaml_settings)
    postgres_cursor = postgres_client.cursor()

    # collect ssim and rebuffer
    ssim_mean, ssim_diff = collect_ssim(influx_client,
                                        expt_id_cache, postgres_cursor, args)
    plot_ssim(ssim_mean, ssim_diff, args)

    postgres_cursor.close()


if __name__ == '__main__':
    main()
