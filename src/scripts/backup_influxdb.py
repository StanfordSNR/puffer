#!/usr/bin/env python3

import os
import sys
import time
import math
import yaml
import requests
import argparse
from datetime import datetime, timedelta
from helpers import check_call


backup_hour = 11  # back up at 11 AM (UTC) every day

ZULIP_URL = os.environ['ZULIP_URL']
ZULIP_BOT_EMAIL = os.environ['ZULIP_BOT_EMAIL']
ZULIP_BOT_TOKEN = os.environ['ZULIP_BOT_TOKEN']


def post_to_zulip(retcode, dst_dir):
    if retcode != 0:
        content = ('**Data Release Error** :warning: '
                   '[{}](https://puffer.stanford.edu/results/)')
        payload = [
            ('type', 'stream'),
            ('to', 'puffer-alert'),
            ('subject', 'Alert'),
            ('content', content.format(dst_dir)),
        ]
    else:
        content = ('**Data Release Success** :check_mark: '
                   '[{}](https://puffer.stanford.edu/results/)')
        payload = [
            ('type', 'stream'),
            ('to', 'puffer-notification'),
            ('subject', 'Daily Report'),
            ('content', content.format(dst_dir)),
        ]

    requests.post(
        ZULIP_URL, data=payload,
        auth=(ZULIP_BOT_EMAIL, ZULIP_BOT_TOKEN))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('yaml_settings')
    args = parser.parse_args()

    with open(args.yaml_settings, 'r') as fh:
        yaml_settings = yaml.safe_load(fh)

    # the script is preferred to be run after 'backup_hour' AM (UTC)
    ts = datetime.utcnow()
    end_ts = datetime(ts.year, ts.month, ts.day, backup_hour, 0)

    # in case this script is executed before 'backup_hour' AM (UTC)
    if ts.hour < backup_hour:
        sys.stderr.write('Warning: sleeping until {} AM (UTC) is passed\n'
                         .format(backup_hour))
        time.sleep(math.ceil((end_ts - ts).total_seconds()))

    # back up the data collected in the last 24 hours
    start_ts = end_ts - timedelta(days=1)

    # convert datetime to time strings
    time_str = '%Y-%m-%dT%H:%M:%SZ'
    short_time_str = '%Y-%m-%dT%H'

    end_ts_str = end_ts.strftime(time_str)
    start_ts_str = start_ts.strftime(time_str)

    dst_dir = start_ts.strftime(short_time_str) + '_' + end_ts.strftime(short_time_str)

    # back up InfluxDB
    influx = yaml_settings['influxdb_connection']
    cmd = ('influxd backup -portable -database {} -start {} -end {} {}'
           .format(influx['dbname'], start_ts_str, end_ts_str, dst_dir))
    check_call(cmd, shell=True)

    # compress dst_dir
    cmd = 'tar czvf {0}.tar.gz {0}'.format(dst_dir)
    check_call(cmd, shell=True)

    # upload to Google cloud
    cmd = 'gsutil cp {}.tar.gz gs://puffer-influxdb-analytics'.format(dst_dir)
    check_call(cmd, shell=True)

    # remove files
    cmd = 'rm -rf {0} {0}.tar.gz'.format(dst_dir)
    check_call(cmd, shell=True)

    # read from YAML the path to Emily's program and run it
    retcode = call(yaml_settings['data_release_script'], shell=True)
    post_to_zulip(retcode, dst_dir)


if __name__ == '__main__':
    main()
