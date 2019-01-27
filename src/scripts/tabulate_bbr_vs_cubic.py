#!/usr/bin/env python3

import sys
import yaml
import argparse
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

from helpers import (
    connect_to_postgres, connect_to_influxdb, retrieve_expt_config,
    ssim_index_to_db, get_ssim_index, query_measurement, get_abr_cc)
from collect_data import (
    video_data_by_session, buffer_data_by_session, VIDEO_DURATION)


def get_video_data(influx_client, args):
    video_sent_results = query_measurement(influx_client, 'video_sent',
                                           args.time_start, args.time_end)
    video_acked_results = query_measurement(influx_client, 'video_acked',
                                            args.time_start, args.time_end)

    video_data = video_data_by_session(video_sent_results, video_acked_results)

    return video_data


def collect_ssim(influx_client, expt_id_cache, postgres_cursor, args):
    d = get_video_data(influx_client, args)

    # index by abr, they by cc
    ssim_mean = {}
    ssim_diff = {}

    for session in d:
        expt_id = session[-1]
        expt_config = retrieve_expt_config(expt_id, expt_id_cache,
                                           postgres_cursor)
        abr, cc = get_abr_cc(expt_config)

        if abr not in ssim_mean:
            ssim_mean[abr] = {}
            ssim_diff[abr] = {}

        if cc not in ssim_mean[abr]:
            ssim_mean[abr][cc] = []
            ssim_diff[abr][cc] = []

        for video_ts in d[session]:
            dsv = d[session][video_ts]

            # append SSIM
            curr_ssim_index = dsv['ssim_index']
            if curr_ssim_index == 1:
                continue

            curr_ssim_db = ssim_index_to_db(curr_ssim_index)
            ssim_mean[abr][cc].append(curr_ssim_db)

            # append SSIM variation
            prev_ts = video_ts - VIDEO_DURATION
            if prev_ts not in d[session]:
                continue

            prev_ssim_index = d[session][prev_ts]['ssim_index']
            if prev_ssim_index == 1:
                continue

            prev_ssim_db = ssim_index_to_db(prev_ssim_index)
            abs_diff = abs(curr_ssim_db - prev_ssim_db)
            ssim_diff[abr][cc].append(abs_diff)

    for abr in ssim_mean:
        for cc in ssim_mean[abr]:
            ssim_mean[abr][cc] = np.mean(ssim_mean[abr][cc])
            ssim_diff[abr][cc] = np.mean(ssim_diff[abr][cc])

    return ssim_mean, ssim_diff


def tabulate_ssim(influx_client, expt_id_cache, postgres_cursor, args):
    ssim_mean, ssim_diff = collect_ssim(influx_client,
                                        expt_id_cache, postgres_cursor, args)

    print('Increasement of average SSIM (dB)')
    for abr in ssim_mean:
        bbr = ssim_mean[abr]['bbr']
        cubic = ssim_mean[abr]['cubic']
        print('{}: {:.2f}'.format(abr, bbr - cubic))

    print('Decreasement of average SSIM variation (dB)')
    for abr in ssim_diff:
        bbr = ssim_diff[abr]['bbr']
        cubic = ssim_diff[abr]['cubic']
        print('{}: {:.2f}'.format(abr, cubic - bbr))


def get_buffer_data(influx_client, args):
    client_buffer_results = query_measurement(influx_client, 'client_buffer',
                                              args.time_start, args.time_end)

    buffer_data = buffer_data_by_session(client_buffer_results)

    return buffer_data


def collect_rebuffer_rate(influx_client, expt_id_cache, postgres_cursor, args):
    d = get_buffer_data(influx_client, args)

    rebuffer_rate = {}  # index by abr, then by cc

    for session in d:
        expt_id = session[-1]
        expt_config = retrieve_expt_config(expt_id, expt_id_cache,
                                           postgres_cursor)
        abr, cc = get_abr_cc(expt_config)

        if abr not in rebuffer_rate:
            rebuffer_rate[abr] = {}

        if cc not in rebuffer_rate[abr]:
            rebuffer_rate[abr][cc] = []

        ds = d[session]
        rebuffer_rate[abr][cc].append(ds['rebuf'] / ds['play'])

    return rebuffer_rate


def tabulate_rebuffer_rate(influx_client, expt_id_cache, postgres_cursor, args):
    rebuffer_rate = collect_rebuffer_rate(influx_client,
                                          expt_id_cache, postgres_cursor, args)

    for p in [95, 97, 99]:
        print('Reduction of {}th percentile rebuffer rate (%)'.format(p))
        for abr in rebuffer_rate:
            bbr = np.percentile(rebuffer_rate[abr]['bbr'], p)
            cubic = np.percentile(rebuffer_rate[abr]['cubic'], p)
            print('{}: {:.2f}'.format(abr, cubic - bbr))


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

    # create an InfluxDB client and perform queries
    influx_client = connect_to_influxdb(yaml_settings)

    # cache of Postgres data: experiment 'id' -> json 'data' of the experiment
    expt_id_cache = {}

    # create a client connected to Postgres
    postgres_client = connect_to_postgres(yaml_settings)
    postgres_cursor = postgres_client.cursor()

    # tabulate SSIM and its variation
    tabulate_ssim(influx_client, expt_id_cache, postgres_cursor, args)

    # tabulate rebuffer rate
    tabulate_rebuffer_rate(influx_client, expt_id_cache, postgres_cursor, args)

    postgres_cursor.close()


if __name__ == '__main__':
    main()
