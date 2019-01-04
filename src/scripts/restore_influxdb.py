#!/usr/bin/env python3

import sys
import time
import argparse
import yaml
from helpers import check_call, connect_to_influxdb


SRC_DB = 'puffer'
DST_DB = 'puffer_restored'
TMP_DB = 'puffer_tmp'


def sanity_check_influxdb(influx_client):
    db_list = influx_client.get_list_database()
    dst_db_exists = False
    for db in db_list:
        if db['name'] == TMP_DB:
            influx_client.drop_database(TMP_DB)
            sys.stderr.write('Warning: database "{}" is dropped\n'
                             .format(TMP_DB))
        elif db['name'] == DST_DB:
            dst_db_exists = True
    if not dst_db_exists:
        influx_client.create_database(DST_DB)
        sys.stderr.write('Warning: database "{}" is created\n'.format(DST_DB))


def download_untar(file_to_restore):
    # download
    cmd = 'gsutil cp gs://puffer-influxdb-backup/{} .'.format(file_to_restore)
    check_call(cmd, shell=True)

    # untar
    cmd = 'tar xf {}'.format(file_to_restore)
    check_call(cmd, shell=True)

    # remove tar
    cmd = 'rm -f {}'.format(file_to_restore)
    check_call(cmd, shell=True)


def restore(filename, influx_client):
    # restore to a temporary database
    cmd = ('influxd restore -portable -db {} -newdb {} {}'
           .format(SRC_DB, TMP_DB, filename))
    check_call(cmd, shell=True)

    # workaround: sleep for a while to avoid "ERR: engine is closed"
    time.sleep(1)

    # sideload the data into the destination database
    influx_client.switch_database(TMP_DB)
    influx_client.query('SELECT * INTO {}..:MEASUREMENT FROM /.*/ GROUP BY *'
                        .format(DST_DB))
    influx_client.drop_database(TMP_DB)

    # remove decompressed folder
    cmd = 'rm -rf {}'.format(filename)
    check_call(cmd, shell=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('yaml_settings')
    parser.add_argument('file_to_restore',
                        help='e.g., 2018-12-04T11_2018-12-05T11.tar.gz')
    args = parser.parse_args()

    file_to_restore = args.file_to_restore
    filename = file_to_restore[:file_to_restore.index('.')]

    with open(args.yaml_settings, 'r') as fh:
        yaml_settings = yaml.safe_load(fh)

    # connect to InfluxDB
    influx_client = connect_to_influxdb(yaml_settings)

    # sanity checks
    sanity_check_influxdb(influx_client)

    # download data from Google cloud
    download_untar(file_to_restore)

    # restore InfluxDB data
    restore(filename, influx_client)


if __name__ == '__main__':
    main()
