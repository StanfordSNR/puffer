#!/usr/bin/env python3

import yaml
import argparse
from helpers import connect_to_influxdb, datetime_iter, query_measurement


args = None


def do_count_hours_users(influx_client, s_str, e_str, state):
    video_acked_results = query_measurement(influx_client, 'video_acked',
                                            s_str, e_str)

    for pt in video_acked_results['video_acked']:
        state['num_chunk'] += 1
        state['distinct_users'].add(pt['user'])


def count_hours_users(influx_client):
    state = {}
    state['num_chunk'] = 0
    state['distinct_users'] = set()

    for s_str, e_str in datetime_iter(args.start_time, args.end_time):
        do_count_hours_users(influx_client, s_str, e_str, state)

    print('Total hours: {:.2f}'.format(state['num_chunk'] * 2.002 / 3600))
    print('Distinct users: {}'.format(len(state['distinct_users'])))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('yaml_settings')
    parser.add_argument('--from', dest='start_time',
                        help='datetime in UTC conforming to RFC3339')
    parser.add_argument('--to', dest='end_time',
                        help='datetime in UTC conforming to RFC3339')
    global args
    args = parser.parse_args()

    with open(args.yaml_settings, 'r') as fh:
        yaml_settings = yaml.safe_load(fh)

    # create an InfluxDB client and perform queries
    influx_client = connect_to_influxdb(yaml_settings)

    count_hours_users(influx_client)


if __name__ == '__main__':
    main()
