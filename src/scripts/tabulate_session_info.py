#!/usr/bin/env python3

import os
import sys
import argparse
import yaml
from datetime import datetime, timedelta
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

from helpers import (
    connect_to_postgres, connect_to_influxdb, try_parsing_time,
    ssim_index_to_db, retrieve_expt_config, get_ssim_index, get_abr_cc,
    create_time_clause, query_measurement)
from collect_data import buffer_data_by_session


def collect_buffer(influx_client, expt_id_cache, postgres_cursor, args):
    client_buffer_results = query_measurement(influx_client, 'client_buffer',
                                              args.time_start, args.time_end)
    buffer_data = buffer_data_by_session(client_buffer_results)

    return buffer_data


def buffer_by_abr_cc(influx_client, expt_id_cache, postgres_cursor, args):
    d = collect_buffer(influx_client,
                       expt_id_cache, postgres_cursor, args)

    x = {}  # indexed by (abr, cc)

    for session in d:
        expt_id = session[-1]
        expt_config = retrieve_expt_config(expt_id, expt_id_cache,
                                           postgres_cursor)
        abr_cc = get_abr_cc(expt_config)

        if abr_cc not in x:
            x[abr_cc] = {}
            x[abr_cc]['play'] = []
            x[abr_cc]['rebuf'] = []
            x[abr_cc]['startup'] = []

        x[abr_cc]['play'].append(d[session]['play'])
        x[abr_cc]['rebuf'].append(d[session]['rebuf'])
        x[abr_cc]['startup'].append(d[session]['startup'])

    return x


def tabulate_buffer_data(buffer_data, args):
    print('Average session duration (min)')
    for abr_cc in buffer_data:
        mean_play = np.mean(buffer_data[abr_cc]['play'])
        print('{}: {:.2f}'.format(abr_cc, mean_play / 60.0))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('yaml_settings')
    parser.add_argument('--from', dest='time_start',
                        help='datetime in UTC conforming to RFC3339')
    parser.add_argument('--to', dest='time_end',
                        help='datetime in UTC conforming to RFC3339')
    parser.add_argument('-o', '--output', required=True)
    args = parser.parse_args()
    output = args.output

    with open(args.yaml_settings, 'r') as fh:
        yaml_settings = yaml.safe_load(fh)

    # create an InfluxDB client and perform queries
    influx_client = connect_to_influxdb(yaml_settings)

    # cache of Postgres data: experiment 'id' -> json 'data' of the experiment
    expt_id_cache = {}

    # create a Postgres client and perform queries
    postgres_client = connect_to_postgres(yaml_settings)
    postgres_cursor = postgres_client.cursor()

    buffer_data = buffer_by_abr_cc(influx_client,
                                   expt_id_cache, postgres_cursor, args)
    tabulate_buffer_data(buffer_data, args)

    postgres_cursor.close()


if __name__ == '__main__':
    main()
