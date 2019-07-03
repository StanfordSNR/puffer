#!/usr/bin/env python3

import sys
import time
import argparse
import yaml
from os import path
from datetime import datetime, timedelta
from helpers import call, check_call, connect_to_influxdb


backup_hour = 11  # back up at 11 AM (UTC) every day

SRC_DB = 'puffer'  # source database name to restore
TMP_DB = 'puffer_tmp'  # used as a middle ground
DST_DB = None  # to be read from YAML settings


def sanity_check_influxdb(influx_client):
    db_list = influx_client.get_list_database()

    dst_db_exists = False
    for db in db_list:
        if db['name'] == TMP_DB:
            influx_client.drop_database(TMP_DB)
            sys.stderr.write('database "{}" is dropped\n'
                             .format(TMP_DB))
        elif db['name'] == DST_DB:
            dst_db_exists = True

    if not dst_db_exists:
        influx_client.create_database(DST_DB)
        sys.stderr.write('database "{}" is created\n'.format(DST_DB))

    sys.stderr.write('Destination database: {}\n'.format(DST_DB))


def get_files_to_restore(start_date, end_date):
    date_format = '%Y-%m-%dT%H'
    start_date = datetime.strptime(start_date, date_format)
    end_date = datetime.strptime(end_date, date_format)

    if end_date <= start_date:
        sys.exit('END_DATE precedes START_DATE')

    # check if all the files to restore exist on Google cloud
    sys.stderr.write('Checking if files to restore exist on cloud...\n')
    ret = []

    s = start_date
    while True:
        e = s + timedelta(days=1)
        if e > end_date:
            break

        f = s.strftime(date_format) + '_' + e.strftime(date_format) + '.tar.gz'

        cmd = 'gsutil -q stat gs://puffer-influxdb-backup/{}'.format(f)
        if call(cmd, shell=True) != 0:
            sys.exit('Error: {} does not exist on cloud'.format(f))
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
            sys.stderr.write('Successfully restored data in {}'.format(d))
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
    files_to_restore = get_files_to_restore(start_date, end_date)

    for f in files_to_restore:
        restore(f, influx_client)


if __name__ == '__main__':
    main()
