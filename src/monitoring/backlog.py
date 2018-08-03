#!/usr/bin/env python3

import os
from os import path
import sys
import time
import argparse
import sched
import math

from datetime import datetime
from influxdb import InfluxDBClient


INFLUX_PWD = os.getenv('INFLUXDB_PASSWORD')
PERIOD = 10  # seconds

scheduler = sched.scheduler(time.time)
influxdb_client = InfluxDBClient('localhost', 8086, 'admin', INFLUX_PWD)


def send_to_influx(total_file_count):
    json_body = [{
        'measurement': 'backlog',
        'time': datetime.utcnow().strftime('%Y-%m-%dT%H:%M:%SZ'),
        'fields': {'total_cnt': total_file_count}
    }]

    sys.stderr.write('total file count {}\n'.format(total_file_count))
    influxdb_client.write_points(json_body, time_precision='s',
                                 database='collectd')


def count_backlog(target_dir):
    total_file_count = sum([len(files) for r, d, files in os.walk(target_dir)])
    send_to_influx(total_file_count)

    # run count_backlog every 60 seconds without drifting
    scheduler.enterabs(PERIOD * math.ceil(time.time() / PERIOD), 1, # priority
                       count_backlog, argument=(target_dir,))
    scheduler.run()



def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('target_dir',
                        help='directory to count files recursively')
    target_dir = parser.parse_args().target_dir

    # schedule the first run
    scheduler.enterabs(PERIOD * math.ceil(time.time() / PERIOD), 1, # priority
                       count_backlog, argument=(target_dir,))
    scheduler.run()


if __name__ == '__main__':
    main()
