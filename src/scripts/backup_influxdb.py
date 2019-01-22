#!/usr/bin/env python3

import sys
import time
import math
import yaml
import argparse
from datetime import datetime, timedelta
from helpers import check_call


backup_hour = 11  # back up at 11 AM (UTC) every day


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
    cmd = 'gsutil cp {}.tar.gz gs://puffer-influxdb-backup'.format(dst_dir)
    check_call(cmd, shell=True)

    # remove files
    cmd = 'rm -rf {0} {0}.tar.gz'.format(dst_dir)
    check_call(cmd, shell=True)


if __name__ == '__main__':
    main()
