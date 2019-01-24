#!/usr/bin/env python3

import os
import sys
import argparse
import yaml
from datetime import datetime, timedelta
import numpy as np
import matplotlib
import matplotlib.pyplot as plt
from helpers import (
    connect_to_influxdb, try_parsing_time,
    ssim_index_to_db, retrieve_expt_config)
matplotlib.use('Agg')

# This file currently generates a plot of ssim vs bitrate for all chunks
# sent by puffer over a one day period.


def collect_ssim_and_bitrate(video_sent_results):
    # process InfluxDB data
    ssims_absolute = []
    ssims_db = []
    bitrates = []
    for pt in video_sent_results['video_sent']:
        if pt['ssim_index'] is not None:
            ssim_index = float(pt['ssim_index'])
            if (ssim_index == 1):
                ssim_index = 1 - 0.000000001  # prevent division by 0 err
            if pt['size'] is None:
                sys.exit('Found db entry with SSIM but not size')

            bitrate_kbps = pt['size'] * PKT_BYTES * 8 / 2.002 / 1000
            if bitrate_kbps > 30000:
                continue
            ssims_absolute.append(ssim_index)
            ssims_db.append(ssim_index_to_db(ssim_index))
            bitrates.append(bitrate_kbps)  # kbps

    return ssims_absolute, ssims_db, bitrates


def plot_ssim_v_bitrate(ssim, bitrate, output, hours, unit):
    time_str = '%Y-%m-%d'
    curr_ts = datetime.utcnow()
    start_ts = curr_ts - timedelta(hours=hours)
    curr_ts_str = curr_ts.strftime(time_str)
    start_ts_str = start_ts.strftime(time_str)

    title = ('SSIM vs bitrate from [{}, {}) (UTC)'
             .format(start_ts_str, curr_ts_str))

    fig, ax = plt.subplots()
    ax.set_title(title)
    ax.set_xlabel('Bitrate (kbps)')
    if unit == "absolute":
        ax.set_ylabel('Average SSIM index')
    else:
        ax.set_ylabel('Average SSIM (dB)')
    ax.grid()

    for idx, ssim_val in enumerate(ssim):
        x = bitrate[idx]
        y = ssim_val
        ax.scatter(x, y)

    fig.savefig(output, dpi=300, bbox_inches='tight', pad_inches=0.2)
    sys.stderr.write('Saved plot to {}\n'.format(output))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('yaml_settings')
    parser.add_argument('-o', '--output', required=True)
    parser.add_argument('-t', '--hours', type=int, default=1)
    args = parser.parse_args()
    output = args.output
    hours = args.hours

    if hours < 1:
        sys.exit('-t/--hours must be a positive integer')

    with open(args.yaml_settings, 'r') as fh:
        yaml_settings = yaml.safe_load(fh)

    # create an InfluxDB client and perform queries
    influx_client = connect_to_influxdb(yaml_settings)

    # query video_sent and client_buffer
    video_sent_results = influx_client.query(
        'SELECT * FROM video_sent WHERE time >= now() - {}h'.format(hours))

    # collect ssim and rebuffer
    ssims_absolute, ssims_db, bitrates = collect_ssim_and_bitrate(video_sent_results)

    if not ssims_absolute:
        sys.exit('Error: no data found in the queried range')

    # plot ssim_absolute vs rebuffer
    plot_ssim_v_bitrate(ssims_absolute, bitrates, output + "_absolute.png",
                        hours, "absolute")

    # plot ssim_db vs rebuffer
    plot_ssim_v_bitrate(ssims_db, bitrates, output + "_db.png", hours, "db")


if __name__ == '__main__':
    main()
