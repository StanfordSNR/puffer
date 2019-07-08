#!/usr/bin/env python3

import sys
import argparse
import yaml
import time
import numpy as np
import os
from os import path
from datetime import datetime, timedelta
from restore_influxdb import download_untar
from subprocess import check_call
from helpers import connect_to_influxdb


backup_hour = 11  # back up at 11 AM (UTC) every day

SRC_DB = 'puffer'  # source database name to restore
TMP_DB = 'puffer_converting'  # temporary database
DST_DB = 'puffer'  # database after converting


tag_keys = {
    'active_streams': ['channel', 'server_id'],
    'backlog': ['channel'],
    'channel_status': ['channel'],
    'client_buffer': ['channel', 'server_id'],
    'client_error': [],
    'client_sysinfo': ['server_id'],
    'decoder_info': ['channel'],
    'server_info': ['server_id'],
    'ssim': ['channel', 'format'],
    'video_acked': ['channel', 'server_id'],
    'video_sent': ['channel', 'server_id'],
    'video_size': ['channel', 'format'],
}

field_keys = {
    'active_streams': {'count': int, 'expt_id': int},
    'backlog': {'canonical_cnt': int, 'working_cnt': int},
    'channel_status': {'selected_rate': float, 'snr': float},
    'client_buffer': {'buffer': float,
                      'cum_rebuf': float,
                      'event': str,
                      'expt_id': int,
                      'init_id': int,
                      'user': str},
    'client_error': {'error': str, 'init_id': int, 'user': str},
    'client_sysinfo': {'browser': str,
                       'expt_id': int,
                       'init_id': int,
                       'ip': str,
                       'os': str,
                       'screen_height': int,
                       'screen_width': int,
                       'user': str},
    'decoder_info': {'due': int, 'filler_fields': int, 'timestamp': int},
    'server_info': {'server_id_1': int},
    'ssim': {'ssim_index': float, 'timestamp': int},
    'video_acked': {'buffer': float,
                    'cum_rebuffer': float,
                    'expt_id': int,
                    'init_id': int,
                    'ssim_index': float,
                    'user': str,
                    'video_ts': int},
    'video_sent': {'buffer': float,
                   'cum_rebuffer': float,
                   'cwnd': int,
                   'delivery_rate': int,
                   'expt_id': int,
                   'format': str,
                   'in_flight': int,
                   'init_id': int,
                   'min_rtt': int,
                   'rtt': int,
                   'size': int,
                   'ssim_index': float,
                   'user': str,
                   'video_ts': int},
    'video_size': {'size': int, 'timestamp': int},
}


def convert_measurement(measurement_name, influx_client):
    sys.stderr.write('Converting measurement {}...\n'.format(measurement_name))
    results = influx_client.query('SELECT * FROM {}'.format(measurement_name))

    json_body = []
    dup_check = set()

    for pt in results[measurement_name]:
        # duplicate check
        fake_server_id = None
        fake_server_id_idx = None

        series = [np.datetime64(pt['time'])]
        for tag_key in tag_keys[measurement_name]:
            if tag_key in pt:
                series.append(str(pt[tag_key]))
            else:
                if tag_key != 'server_id':
                    sys.exit('{} does not exist in data point'.format(tag_key))

                fake_server_id = 1
                series.append(str(fake_server_id))
                fake_server_id_idx = len(series) - 1

        while tuple(series) in dup_check:
            if fake_server_id is None:
                sys.exit('Should not happen')

            fake_server_id += 1
            series[fake_server_id_idx] = str(fake_server_id)

        dup_check.add(tuple(series))

        # create tags and fields
        time = str(series[0])
        tags = {}
        fields = {}

        if fake_server_id is not None:
            tags['server_id'] = str(fake_server_id)

        for pt_k in pt:
            if pt_k == 'time':
                continue

            if pt[pt_k] is None:
                continue

            k = pt_k
            if measurement_name != 'server_info':
                if k[-2:] == '_1':
                    k = k[:-2]

            if k in tag_keys[measurement_name]:
                tags[k] = str(pt[pt_k])
            else:
                # convert to correct type
                fields[k] = field_keys[measurement_name][k](pt[pt_k])

        json_body.append({
            'measurement': measurement_name,
            'time': time,
            'tags': tags,
            'fields': fields,
        })

        if len(json_body) >= 1000:
            influx_client.write_points(json_body, database=DST_DB)
            json_body = []

    if json_body:
        influx_client.write_points(json_body, database=DST_DB)


def convert(f, influx_client):
    influx_client.drop_database(TMP_DB)
    influx_client.drop_database(DST_DB)
    influx_client.create_database(TMP_DB)
    influx_client.create_database(DST_DB)

    sys.stderr.write('Converting {}...\n'.format(f))

    # download data from Google cloud
    d = download_untar(f)

    # restore to a temporary database
    cmd = ('influxd restore -portable -db {} -newdb {} {}'
           .format(SRC_DB, TMP_DB, d))
    influx_client.drop_database(TMP_DB)
    check_call(cmd, shell=True)

    influx_client.switch_database(TMP_DB)

    # workaround: sleep for a while to avoid influxdb errors
    # possible errors: shard is disabled, engine is closed
    time.sleep(1)

    measurements = influx_client.get_list_measurements()
    for measurement in measurements:
        measurement_name = measurement['name']
        convert_measurement(measurement_name, influx_client)

    # back up and upload to Google cloud
    dst_root = 'complete'
    if not path.isdir(dst_root):
        os.makedirs(dst_root)
    dst_dir = path.join(dst_root, d)

    cmd = ('influxd backup -portable -database {} {}'
           .format(DST_DB, dst_dir))
    check_call(cmd, shell=True)

    # compress dst_dir
    cmd = 'tar czvf {0}.tar.gz {0}'.format(dst_dir)
    check_call(cmd, shell=True)

    # upload to Google cloud
    cmd = 'gsutil cp {}.tar.gz gs://puffer-influxdb-analytics'.format(dst_dir)
    check_call(cmd, shell=True)


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

    # connect to InfluxDB
    influx_client = connect_to_influxdb(yaml_settings)

    start_date = args.start_date + 'T{}'.format(backup_hour)
    end_date = args.end_date + 'T{}'.format(backup_hour)

    date_format = '%Y-%m-%dT%H'
    start_date = datetime.strptime(start_date, date_format)
    end_date = datetime.strptime(end_date, date_format)

    if end_date <= start_date:
        sys.exit('END_DATE precedes START_DATE')

    s = start_date
    while True:
        e = s + timedelta(days=1)
        if e > end_date:
            break

        s_str = s.strftime(date_format)
        e_str = e.strftime(date_format)
        f = s_str + '_' + e_str + '.tar.gz'

        # convert each day of data
        convert(f, influx_client)

        s = e


if __name__ == '__main__':
    main()
