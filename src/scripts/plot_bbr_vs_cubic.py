#!/usr/bin/env python3

import yaml
import argparse

from helpers import (
    prepare_raw_data, connect_to_postgres, retrieve_expt_config,
    ssim_index_to_db, VIDEO_DURATION)


# cache of Postgres data: experiment 'id' -> json 'data' of the experiment
expt_id_cache = {}


def plot_ssim(d, postgres_cursor):
    data = {
        'cubic': {'ssim': [], 'ssim_var': []},
        'bbr': {'ssim': [], 'ssim_var': []}
    }

    ssim_min = float('inf')
    ssim_max = float('-inf')
    ssim_var_min = float('inf')
    ssim_var_max = float('-inf')

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

            ssim_min = min(ssim_min, curr_ssim_db)
            ssim_max = max(ssim_max, curr_ssim_db)

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

            ssim_var_min = min(ssim_var_min, ssim_diff)
            ssim_var_max = max(ssim_var_max, ssim_diff)

    # TODO plot CDF of SSIM and SSIM variation

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

    # query InfluxDB and retrieve raw data (without filtering by cc)
    raw_data = prepare_raw_data(yaml_settings_path,
                                args.time_start, args.time_end, None)

    # create a client connected to Postgres
    postgres_client = connect_to_postgres(yaml_settings)
    postgres_cursor = postgres_client.cursor()

    # plot CDF of SSIM and SSIM variation
    plot_ssim(raw_data, postgres_cursor)

    postgres_cursor.close()


if __name__ == '__main__':
    main()
