#!/usr/bin/env python3

import sys
import time
import argparse
import yaml
from os import path
from datetime import datetime, timedelta
from subprocess import call, check_call
from helpers import connect_to_influxdb, create_time_clause


backup_hour = 11  # back up at 11 AM (UTC) every day

SRC_DB = 'puffer'  # source database name to restore
TMP_DB = 'puffer_tmp'  # used as a middle ground
DST_DB = None  # to be read from YAML settings

args = None


def sanity_check_influxdb(influx_client):
    db_list = influx_client.get_list_database()

    dst_db_exists = False
    for db in db_list:
        if db['name'] == TMP_DB:
            influx_client.drop_database(TMP_DB)
            sys.stderr.write('Warning: middle ground database "{}" '
                             'is dropped\n'.format(TMP_DB))
        elif db['name'] == DST_DB:
            dst_db_exists = True

    if not dst_db_exists:
        influx_client.create_database(DST_DB)
        sys.stderr.write('Warning: destination database "{}" is created\n'
                         .format(DST_DB))


def get_files_to_restore(start_date, end_date, influx_client):
    date_format = '%Y-%m-%dT%H'
    start_date = datetime.strptime(start_date, date_format)
    end_date = datetime.strptime(end_date, date_format)

    if end_date <= start_date:
        sys.exit('END_DATE precedes START_DATE')

    ret = []

    s = start_date
    while True:
        e = s + timedelta(days=1)
        if e > end_date:
            break

        s_str = s.strftime(date_format)
        e_str = e.strftime(date_format)
        f = s_str + '_' + e_str + '.tar.gz'

        # check if the file to restore exists on cloud
        cmd = 'gsutil -q stat gs://puffer-influxdb-backup/{}'.format(f)
        if call(cmd, shell=True) != 0:
            sys.exit('Error: {} does not exist on cloud'.format(f))

        # check if the data within the range already exists in InfluxDB
        time_clause = create_time_clause(s, e)
        results = influx_client.query(
            'SELECT count(video_ts) FROM video_acked WHERE ' + time_clause)

        count = None
        for pt in results['video_acked']:
            if count is not None:
                sys.exit('Error: query should only return only record')

            count = pt['count']
            sys.stderr.write('Warning: {} records found within {} - {}\n'
                             .format(count, s_str, e_str))

            if args.dry_run:
                break

            if args.allow_skipping:
                sys.stderr.write('Warning: allows skipping\n')
            else:
                sys.exit('Does not allow skipping')

        if count is None:
            sys.stderr.write('Will restore {}\n'.format(f))
            ret.append(f)

        s = e

    return ret


def download_untar(f):
    d = f[:f.index('.')]
    if path.isdir(d):
        sys.stderr.write('Found {} in the current directory\n'.format(d))
        return d

    # download
    cmd = 'gsutil cp gs://puffer-influxdb-backup/{} .'.format(f)
    check_call(cmd, shell=True)

    # untar
    cmd = 'tar xf {}'.format(f)
    check_call(cmd, shell=True)

    # return uncompressed folder name
    return d


def restore(f, influx_client):
    sys.stderr.write('Restoring {}...\n'.format(f))

    # download data from Google cloud
    d = download_untar(f)

    # restore to a temporary database (with retries)
    for retry in range(1, 3):
        cmd = ('influxd restore -portable -db {} -newdb {} {}'
               .format(SRC_DB, TMP_DB, d))
        if call(cmd, shell=True) != 0:
            continue

        influx_client.switch_database(TMP_DB)

        # workaround: sleep for a while to avoid influxdb errors
        # possible errors: shard is disabled, engine is closed
        time.sleep(retry)

        try:
            # sideload the data into the destination database (with retries)
            influx_client.query(
                'SELECT * INTO {}..:MEASUREMENT FROM /.*/ GROUP BY *'
                .format(DST_DB))
            sys.stderr.write('Successfully restored data in {}\n'.format(d))
            return
        except (KeyboardInterrupt, SystemExit):
            raise
        except Exception as e:
            print('Error:', e, file=sys.stderr)
            sys.stderr.write('Retrying...\n')
        finally:
            influx_client.drop_database(TMP_DB)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('yaml_settings')
    parser.add_argument('--from', required=True, dest='start_date',
                        help='e.g., "2019-04-03" ({} AM in UTC)'.format(backup_hour))
    parser.add_argument('--to', required=True, dest='end_date',
                        help='e.g., "2019-04-05" ({} AM in UTC)'.format(backup_hour))
    parser.add_argument('--allow-skipping', action='store_true',
                        help='allow skipping a day if data already exists in InfluxDB')
    parser.add_argument('--dry-run', action='store_true',
                        help='only check and print the status of InfluxDB')
    global args
    args = parser.parse_args()

    with open(args.yaml_settings, 'r') as fh:
        yaml_settings = yaml.safe_load(fh)

    global DST_DB
    DST_DB = yaml_settings['influxdb_connection']['dbname']

    # connect to InfluxDB
    influx_client = connect_to_influxdb(yaml_settings)
    sanity_check_influxdb(influx_client)

    # parse input dates and get valid files to restore
    start_date = args.start_date + 'T{}'.format(backup_hour)
    end_date = args.end_date + 'T{}'.format(backup_hour)
    files_to_restore = get_files_to_restore(start_date, end_date, influx_client)

    if not args.dry_run:
        for f in files_to_restore:
            restore(f, influx_client)


if __name__ == '__main__':
    main()
