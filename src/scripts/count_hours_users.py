#!/usr/bin/env python3

import yaml
import argparse

from helpers import connect_to_influxdb


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('yaml_settings')
    args = parser.parse_args()

    with open(args.yaml_settings, 'r') as fh:
        yaml_settings = yaml.safe_load(fh)

    influx_client = connect_to_influxdb(yaml_settings)

    results = influx_client.query(
        "SELECT \"user\",video_ts FROM video_acked "
        "where time <= '2019-01-28T11:00:00Z'")

    chunk_cnt = 0
    distinct_users = set()
    for result in results['video_acked']:
        chunk_cnt += 1
        distinct_users.add(result['user'])

    print('TOTAL HOURS', chunk_cnt * 2.002 / 3600.0)
    print('DINSTINCT USERS', len(distinct_users))


if __name__ == '__main__':
    main()
