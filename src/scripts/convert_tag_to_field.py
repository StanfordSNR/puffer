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
from subprocess import call, check_call
from helpers import connect_to_influxdb


backup_hour = 11  # back up at 11 AM (UTC) every day

date_format = '%Y-%m-%dT%H'
time_format = '%Y-%m-%dT%H:%M:%SZ'

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

        series = [str(pt['time'])]
        for tag_key in tag_keys[measurement_name]:
            if tag_key in pt and pt[tag_key] is not None:
                series.append(str(pt[tag_key]))
            else:
                # the only possible missing tag key is 'server_id' in
                # 'client_buffer', 'video_sent' or 'video_acked'
                if (tag_key != 'server_id' or
                    (measurement_name != 'client_buffer' and
                     measurement_name != 'video_sent' and
                     measurement_name != 'video_acked')):
                    print(pt, file=sys.stderr)
                    sys.exit('{} does not exist in data point'.format(tag_key))

                fake_server_id = 1
                series.append(str(fake_server_id))
                fake_server_id_idx = len(series) - 1

        # adjust fake_server_id or timestamp to make sure series is unique
        while tuple(series) in dup_check:
            if fake_server_id is not None:
                fake_server_id += 1
                series[fake_server_id_idx] = str(fake_server_id)
            else:
                if (measurement_name == 'client_buffer' or
                    measurement_name == 'video_sent' or
                    measurement_name == 'video_acked'):
                    print(pt, file=sys.stderr)
                    sys.exit('Should not need to adjust timestamp in {}'
                             .format(measurement_name))

                np_time = np.datetime64(series[0], 'us')
                np_time += np.timedelta64(1, 'us')
                series[0] = str(np_time)
                sys.stderr.write('Timestamp is incremented to {}\n'
                                 .format(str(np_time)))

        dup_check.add(tuple(series))

        # create tags and fields
        time = series[0]
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
            elif k in field_keys[measurement_name]:
                # convert to correct type
                fields[k] = field_keys[measurement_name][k](pt[pt_k])
            else:
                print(pt, file=sys.stderr)
                sys.exit('{} is not a tag or a field'.format(k))

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


def download_from_backup(f):
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


def convert(s, e, influx_client):
    f = s.strftime(date_format) + '_' + e.strftime(date_format) + '.tar.gz'

    sys.stderr.write('Converting {}...\n'.format(f))

    # check if f already exists in puffer-influxdb-analytics
    cmd = 'gsutil -q stat gs://puffer-influxdb-analytics/{}'.format(f)
    if call(cmd, shell=True) == 0:
        sys.stderr.write('{} already exists in gs://puffer-influxdb-analytics/\n'
                         .format(f))
        return

    # download f from puffer-influxdb-backup
    d = download_from_backup(f)

    # clean start
    influx_client.drop_database(DST_DB)
    influx_client.create_database(DST_DB)

    # restore to a temporary database
    for retry in range(4):
        influx_client.drop_database(TMP_DB)

        cmd = ('influxd restore -portable -db {} -newdb {} {}'
               .format(SRC_DB, TMP_DB, d))
        if call(cmd, shell=True) != 0:
            continue

        influx_client.switch_database(TMP_DB)

        # workaround: sleep for a while to avoid influxdb errors
        # possible errors: shard is disabled, engine is closed
        time.sleep(2 ** retry)

        try:
            measurements = influx_client.get_list_measurements()
            break
        except (KeyboardInterrupt, SystemExit):
            raise
        except Exception as e:
            print('Error:', e, file=sys.stderr)
            sys.stderr.write('Retrying...\n')

    for measurement in measurements:
        measurement_name = measurement['name']
        convert_measurement(measurement_name, influx_client)

    # back up and upload to Google cloud
    complete = 'complete'
    if not path.isdir(complete):
        os.makedirs(complete)

    cmd = ('influxd backup -portable -database {} -start {} -end {} {}'
           .format(DST_DB, s.strftime(time_format), e.strftime(time_format), d))
    check_call(cmd, shell=True, cwd=complete)

    # compress dst_dir
    cmd = 'tar czvf {0}.tar.gz {0}'.format(d)
    check_call(cmd, shell=True, cwd=complete)

    # upload to Google cloud
    cmd = 'gsutil cp {}.tar.gz gs://puffer-influxdb-analytics'.format(d)
    check_call(cmd, shell=True, cwd=complete)


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

    start_date = datetime.strptime(start_date, date_format)
    end_date = datetime.strptime(end_date, date_format)

    if end_date <= start_date:
        sys.exit('END_DATE precedes START_DATE')

    s = start_date
    while True:
        e = s + timedelta(days=1)
        if e > end_date:
            break

        # convert each day of data
        convert(s, e, influx_client)

        s = e


if __name__ == '__main__':
    main()
