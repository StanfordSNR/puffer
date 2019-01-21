#!/usr/bin/env python3

import argparse

from helpers import prepare_raw_data


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('yaml_settings')
    parser.add_argument('--from', dest='time_start',
                        help='datetime in UTC conforming to RFC3339')
    parser.add_argument('--to', dest='time_end',
                        help='datetime in UTC conforming to RFC3339')
    args = parser.parse_args()

    # query InfluxDB and retrieve raw data (without filtering by cc)
    raw_data = prepare_raw_data(args.yaml_settings,
                                args.time_start, args.time_end, None)


if __name__ == '__main__':
    main()
