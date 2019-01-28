#!/usr/bin/env python3

import sys
import yaml
import pickle
import argparse
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

from helpers import (
    connect_to_postgres, ssim_index_to_db, retrieve_expt_config, get_abr_cc)
from collect_data import VIDEO_DURATION


def collect_ssim(d, expt_id_cache, postgres_cursor):
    ssim_mean = {}
    ssim_diff = {}

    for session in d:
        expt_id = session[-1]
        expt_config = retrieve_expt_config(expt_id, expt_id_cache,
                                           postgres_cursor)
        abr_cc = get_abr_cc(expt_config)

        if abr_cc not in ssim_mean:
            ssim_mean[abr_cc] = []
            ssim_diff[abr_cc] = []

        for video_ts in d[session]:
            dsv = d[session][video_ts]

            curr_ssim_index = dsv['ssim_index']
            if curr_ssim_index == 1:
                continue
            curr_ssim_db = ssim_index_to_db(curr_ssim_index)

            # append SSIM index
            ssim_mean[abr_cc].append(curr_ssim_index)

            # append SSIM variation
            prev_ts = video_ts - VIDEO_DURATION
            if prev_ts not in d[session]:
                continue

            prev_ssim_index = d[session][prev_ts]['ssim_index']
            if prev_ssim_index == 1:
                continue
            prev_ssim_db = ssim_index_to_db(prev_ssim_index)

            ssim_diff[abr_cc].append(abs(curr_ssim_db - prev_ssim_db))

    for abr_cc in ssim_mean:
        ssim_mean[abr_cc] = ssim_index_to_db(np.mean(ssim_mean[abr_cc]))
        ssim_diff[abr_cc] = np.mean(ssim_diff[abr_cc])

    return ssim_mean, ssim_diff


def collect_rebuffer(d, expt_id_cache, postgres_cursor):
    total_play = {}
    total_rebuf = {}
    startup = {}

    for session in d:
        expt_id = session[-1]
        expt_config = retrieve_expt_config(expt_id, expt_id_cache,
                                           postgres_cursor)
        abr_cc = get_abr_cc(expt_config)
        if abr_cc not in total_play:
            total_play[abr_cc] = 0
            total_rebuf[abr_cc] = 0
            startup[abr_cc] = []

        total_play[abr_cc] += d[session]['play']
        total_rebuf[abr_cc] += d[session]['rebuf']
        startup[abr_cc].append(d[session]['startup'])

    rebuffer_rate = {}

    for abr_cc in startup:
        rebuffer_rate[abr_cc] = total_rebuf[abr_cc] / total_play[abr_cc]
        startup[abr_cc] = np.mean(startup[abr_cc])

    return rebuffer_rate, startup


def plot_ssim_mean_vs_rebuf_rate(ssim_mean, rebuffer_rate, args):
    fig, ax = plt.subplots()

    ax.set_xlabel('Time spent stalled (%)')
    ax.set_ylabel('Average SSIM (dB)')
    ax.grid()

    for abr_cc in ssim_mean:
        abr_cc_str = '{}+{}'.format(*abr_cc)
        if abr_cc not in rebuffer_rate:
            sys.exit('Error: {} does not exist in rebuffer'
                     .format(abr_cc))

        x = rebuffer_rate[abr_cc] * 100  # %
        y = ssim_mean[abr_cc]
        print(abr_cc, x, y)

        ax.scatter(x, y)
        ax.annotate(abr_cc_str, (x, y))

    # clamp x-axis to [0, 100]
    xmin, xmax = ax.get_xlim()
    xmin = max(xmin, 0)
    xmax = min(xmax, 100)
    ax.set_xlim(xmin, xmax)
    ax.invert_xaxis()

    fig.savefig(args.output)
    sys.stderr.write('Saved plot to {}\n'.format(args.output))


def plot(expt_id_cache, postgres_cursor, args):
    with open(args.video_data_pickle, 'rb') as fh:
        video_data = pickle.load(fh)

    with open(args.buffer_data_pickle, 'rb') as fh:
        buffer_data = pickle.load(fh)

    ssim_mean, ssim_diff = collect_ssim(video_data,
                                        expt_id_cache, postgres_cursor)
    rebuffer_rate, startup = collect_rebuffer(buffer_data,
                                              expt_id_cache, postgres_cursor)

    plot_ssim_mean_vs_rebuf_rate(ssim_mean, rebuffer_rate, args)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('yaml_settings')
    parser.add_argument('-v', '--video-data-pickle', required=True)
    parser.add_argument('-b', '--buffer-data-pickle', required=True)
    parser.add_argument('-o', '--output', required=True)
    args = parser.parse_args()

    with open(args.yaml_settings, 'r') as fh:
        yaml_settings = yaml.safe_load(fh)

    # cache of Postgres data: experiment 'id' -> json 'data' of the experiment
    expt_id_cache = {}

    # create a Postgres client and perform queries
    postgres_client = connect_to_postgres(yaml_settings)
    postgres_cursor = postgres_client.cursor()

    plot(expt_id_cache, postgres_cursor, args)

    postgres_cursor.close()


if __name__ == '__main__':
    main()
