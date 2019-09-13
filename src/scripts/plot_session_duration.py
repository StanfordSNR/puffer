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
from stream_processor import BufferStream


args = None
expt = {}
influx_client = None

g_duration = {}  # { abr_cc: [] }


def process_session(session, s):
    print(session)

    global g_duration
    # TODO


def collect_session_duration():
    buffer_stream = BufferStream(process_session)
    buffer_stream.process(influx_client, args.start_time, args.end_time)

    return g_duration


def plot_session_duration(duration):
    # TODO
    pass


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('yaml_settings')
    parser.add_argument('--from', dest='start_time',
                        help='datetime in UTC conforming to RFC3339')
    parser.add_argument('--to', dest='end_time',
                        help='datetime in UTC conforming to RFC3339')
    parser.add_argument('--expt', help='e.g., expt_cache.json')
    parser.add_argument('-o', '--output', required=True)
    parser.add_argument('-i', '--input', help='file to read data from')
    global args
    args = parser.parse_args()

    # read data from args.input and exit
    if args.input:
        with open(args.input) as fh:
            sys.stderr.write('Read data from {}\n'.format(args.input))
            duration = eval(fh.readline())
            print(duration)
            plot_session_duration(duration)
            return

    with open(args.yaml_settings) as fh:
        yaml_settings = yaml.safe_load(fh)

    with open(args.expt) as fh:
        global expt
        expt = json.load(fh)

    # create an InfluxDB client and perform queries
    global influx_client
    influx_client = connect_to_influxdb(yaml_settings)

    duration = collect_session_duration()
    print(duration)
    plot_session_duration(duration)


if __name__ == '__main__':
    main()
