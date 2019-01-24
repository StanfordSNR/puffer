#!/usr/bin/env python3

import sys
import yaml
import argparse
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

from helpers import (
    connect_to_postgres, retrieve_expt_config, ssim_index_to_db)
from collect_data import collect_video_data, VIDEO_DURATION


# cache of Postgres data: experiment 'id' -> json 'data' of the experiment
expt_id_cache = {}


def plot_ssim_cdf(data, args):
    fig, ax = plt.subplots()

    x_min = 0
    x_max = 25
    num_bins = 100
    for cc in data:
        counts, bin_edges = np.histogram(data[cc]['ssim'], bins=num_bins,
                                         range=(x_min, x_max))

        x = bin_edges
        y = np.cumsum(counts) / len(data[cc]['ssim'])
        y = np.insert(y, 0, 0)  # prepend 0

        ax.plot(x, y, label=cc)

    ax.set_xlim(x_min, x_max)
    ax.set_ylim(0, 1)
    ax.legend()
    ax.grid()

    title = '[{}, {}] (UTC)'.format(args.time_start, args.time_end)
    ax.set_title(title)
    ax.set_xlabel('SSIM (dB)')
    ax.set_ylabel('CDF')

    figname = 'bbr_cubic_ssim_cdf.png'
    fig.savefig(figname, dpi=150, bbox_inches='tight')
    sys.stderr.write('Saved plot to {}\n'.format(figname))


def plot_ssim_var_cdf(data, args):
    fig, ax = plt.subplots()

    x_min = 0
    x_max = 4
    num_bins = 100
    for cc in data:
        counts, bin_edges = np.histogram(data[cc]['ssim_var'], bins=num_bins,
                                         range=(x_min, x_max))

        x = bin_edges
        y = np.cumsum(counts) / len(data[cc]['ssim_var'])
        y = np.insert(y, 0, 0)  # prepend 0

        ax.plot(x, y, label=cc)

    ax.set_xlim(x_min, x_max)
    ax.set_ylim(0, 1)
    ax.legend()
    ax.grid()

    title = '[{}, {}] (UTC)'.format(args.time_start, args.time_end)
    ax.set_title(title)
    ax.set_xlabel('Absolute SSIM variation')
    ax.set_ylabel('CDF')

    figname = 'bbr_cubic_ssim_var_cdf.png'
    fig.savefig(figname, dpi=150, bbox_inches='tight')
    sys.stderr.write('Saved plot to {}\n'.format(figname))


def plot_ssim(d, postgres_cursor, args):
    data = {
        'cubic': {'ssim': [], 'ssim_var': []},
        'bbr': {'ssim': [], 'ssim_var': []}
    }

    for session in d:
        expt_id = session[3]
        expt_config = retrieve_expt_config(expt_id, expt_id_cache,
                                           postgres_cursor)
        cc = expt_config['cc']
        if cc != 'cubic' and cc != 'bbr':
            continue

        for video_ts in d[session]:
            dsv = d[session][video_ts]

            # append SSIM
            curr_ssim_index = dsv['ssim_index']
            if curr_ssim_index == 1:
                continue

            curr_ssim_db = ssim_index_to_db(curr_ssim_index)
            data[cc]['ssim'].append(curr_ssim_db)

            # append SSIM variation
            prev_ts = video_ts - VIDEO_DURATION
            if prev_ts not in d[session]:
                continue

            prev_ssim_index = d[session][prev_ts]['ssim_index']
            if prev_ssim_index == 1:
                continue

            prev_ssim_db = ssim_index_to_db(prev_ssim_index)
            ssim_diff = abs(curr_ssim_db - prev_ssim_db)
            data[cc]['ssim_var'].append(ssim_diff)

    # plot CDF of SSIM
    plot_ssim_cdf(data, args)

    # plto CDF of SSIM variation
    plot_ssim_var_cdf(data, args)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('yaml_settings')
    parser.add_argument('--from', dest='time_start',
                        help='datetime in UTC conforming to RFC3339')
    parser.add_argument('--to', dest='time_end',
                        help='datetime in UTC conforming to RFC3339')
    args = parser.parse_args()

    yaml_settings_path = args.yaml_settings
    with open(yaml_settings_path, 'r') as fh:
        yaml_settings = yaml.safe_load(fh)

    video_data = collect_video_data(yaml_settings_path,
                                    args.time_start, args.time_end, None)

    # create a client connected to Postgres
    postgres_client = connect_to_postgres(yaml_settings)
    postgres_cursor = postgres_client.cursor()

    # plot SSIM and SSIM variation
    plot_ssim(video_data, postgres_cursor, args)

    postgres_cursor.close()


if __name__ == '__main__':
    main()
