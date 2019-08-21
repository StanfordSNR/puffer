#!/usr/bin/env python3

import os
import sys
import argparse
import yaml
import json
import numpy as np

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

from helpers import connect_to_influxdb
from stream_processor import VideoStreamCallback


VIDEO_DURATION = 180180

args = None
expt = {}
influx_client = None


def process_session(s):
    for curr_ts in s:
        chunks = []

        for i in reversed(range(6)):
            ts = curr_ts - i * VIDEO_DURATION
            if ts not in s:
                break

            chunks.append((s[ts]['size'], s[ts]['trans_time']))

        if len(chunks) != 6:
            continue

        print(chunks)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('yaml_settings')
    parser.add_argument('--from', dest='start_time', required=True,
                        help='datetime in UTC conforming to RFC3339')
    parser.add_argument('--to', dest='end_time', required=True,
                        help='datetime in UTC conforming to RFC3339')
    parser.add_argument('--expt', help='e.g., expt_cache.json')
    parser.add_argument('-o', '--output', required=True)
    global args
    args = parser.parse_args()

    with open(args.yaml_settings, 'r') as fh:
        yaml_settings = yaml.safe_load(fh)

    with open(args.expt, 'r') as fh:
        global expt
        expt = json.load(fh)

    # create an InfluxDB client and perform queries
    global influx_client
    influx_client = connect_to_influxdb(yaml_settings)

    video_stream = VideoStreamCallback(process_session)
    video_stream.process(influx_client, args.start_time, args.end_time)


if __name__ == '__main__':
    main()
