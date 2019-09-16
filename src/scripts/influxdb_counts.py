#!/usr/bin/env python3

import sys
import argparse
import yaml
from datetime import datetime, timedelta
from helpers import connect_to_influxdb, create_time_clause


backup_hour = 11  # back up at 11 AM (UTC) every day

args = None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('yaml_settings')
    parser.add_argument('--from', required=True, dest='start_date',
                        help='e.g., "2019-04-03" ({} AM in UTC)'.format(backup_hour))
    parser.add_argument('--to', required=True, dest='end_date',
                        help='e.g., "2019-04-05" ({} AM in UTC)'.format(backup_hour))
    global args
    args = parser.parse_args()

    with open(args.yaml_settings, 'r') as fh:
        yaml_settings = yaml.safe_load(fh)

    # connect to InfluxDB
    influx_client = connect_to_influxdb(yaml_settings)

    # parse input dates
    start_date = args.start_date + 'T{}'.format(backup_hour)
    end_date = args.end_date + 'T{}'.format(backup_hour)

    date_format = '%Y-%m-%dT%H'
    start_date = datetime.strptime(start_date, date_format)
    end_date = datetime.strptime(end_date, date_format)

    if end_date <= start_date:
        sys.exit('END_DATE precedes START_DATE')

    measurements = ['video_sent', 'video_acked', 'client_buffer']
    field = {
        'video_sent': 'video_ts',
        'video_acked': 'video_ts',
        'client_buffer': 'buffer',
    }

    s = start_date
    while True:
        e = s + timedelta(days=1)
        if e > end_date:
            break

        s_str = s.strftime(date_format)
        e_str = e.strftime(date_format)
        line = s_str + '_' + e_str

        time_clause = create_time_clause(s, e)

        for m in measurements:
            results = influx_client.query(
                'SELECT count({}) FROM {} WHERE {}'
                .format(field[m], m, time_clause))[m]

            for result in results:  # should only return one result
                line += ' {} {}'.format(m, result['count'])
                break

        print(line)

        s = e


if __name__ == '__main__':
    main()
