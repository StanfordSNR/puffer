#!/usr/bin/env python3

import argparse
import yaml
import struct
import numpy as np

import hashlib
import base64

from helpers import datetime_iter, connect_to_influxdb, query_measurement


backup_hour = 11  # back up at 11 AM (UTC) every day

args = None
yaml_settings = None

session_map = {}  # { stream_key: session_id }


def gen_session_id(stream_key):
    m = hashlib.sha256()
    m.update(stream_key[0].encode('utf-8'))
    m.update(struct.pack('>I', stream_key[1]))
    m.update(struct.pack('>I', stream_key[2]))
    return base64.b64encode(m.digest()).decode('ascii')


def find_session_id(stream_key):
    global session_map

    for i in range(1, 11):
        prev_stream_key = (stream_key[0], int(stream_key[1] - i), int(stream_key[2]))
        if prev_stream_key in session_map:
            session_map[stream_key] = session_map[prev_stream_key]
            return session_map[stream_key]

    session_map[stream_key] = gen_session_id(stream_key)
    return session_map[stream_key]


def dump_video_sent(s_str, e_str):
    # connect to InfluxDB
    influx_client = connect_to_influxdb(yaml_settings)

    csv_fname = 'video_sent_{}.csv'.format(s_str[:-7])
    csv_fh = open(csv_fname, 'w')

    video_sent_results = query_measurement(
        influx_client, 'video_sent', s_str, e_str)['video_sent']

    for pt in video_sent_results:
        epoch_ts = np.datetime64(pt['time']).astype('datetime64[ms]').astype('int')
        stream_key = (pt['user'], int(pt['init_id']), int(pt['expt_id']))
        session_id = find_session_id(stream_key)

        csv_fh.write(','.join(map(str, [
            epoch_ts, session_id, pt['expt_id'], pt['channel'], pt['video_ts'],
            pt['format'], pt['size'], pt['ssim_index'],
            pt['cwnd'], pt['in_flight'], pt['min_rtt'], pt['rtt'], pt['delivery_rate']
        ])) + '\n')

    csv_fh.close()


def dump_video_acked(s_str, e_str):
    # connect to InfluxDB
    influx_client = connect_to_influxdb(yaml_settings)

    csv_fname = 'video_acked_{}.csv'.format(s_str[:-7])
    csv_fh = open(csv_fname, 'w')

    video_acked_results = query_measurement(
        influx_client, 'video_acked', s_str, e_str)['video_acked']

    for pt in video_acked_results:
        epoch_ts = np.datetime64(pt['time']).astype('datetime64[ms]').astype('int')
        stream_key = (pt['user'], int(pt['init_id']), int(pt['expt_id']))
        session_id = find_session_id(stream_key)

        csv_fh.write(','.join(map(str, [
            epoch_ts, session_id, pt['expt_id'], pt['channel'], pt['video_ts']
        ])) + '\n')


    csv_fh.close()


def dump_client_buffer(s_str, e_str):
    # connect to InfluxDB
    influx_client = connect_to_influxdb(yaml_settings)

    csv_fname = 'client_buffer_{}.csv'.format(s_str[:-7])
    csv_fh = open(csv_fname, 'w')

    client_buffer_results = query_measurement(
        influx_client, 'client_buffer', s_str, e_str)['client_buffer']

    for pt in client_buffer_results:
        epoch_ts = np.datetime64(pt['time']).astype('datetime64[ms]').astype('int')
        stream_key = (pt['user'], int(pt['init_id']), int(pt['expt_id']))
        session_id = find_session_id(stream_key)

        csv_fh.write(','.join(map(str, [
            epoch_ts, session_id, pt['expt_id'], pt['channel'],
            pt['event'], pt['buffer'], pt['cum_rebuf']
        ])) + '\n')

    csv_fh.close()


def dump_client_sysinfo(s_str, e_str):
    # connect to InfluxDB
    influx_client = connect_to_influxdb(yaml_settings)

    csv_fname = 'client_sysinfo_{}.csv'.format(s_str[:-7])
    csv_fh = open(csv_fname, 'w')

    client_sysinfo_results = query_measurement(
        influx_client, 'client_sysinfo', s_str, e_str)['client_sysinfo']

    for pt in client_sysinfo_results:
        epoch_ts = np.datetime64(pt['time']).astype('datetime64[ms]').astype('int')
        stream_key = (pt['user'], int(pt['init_id']), int(pt['expt_id']))
        session_id = find_session_id(stream_key)

        ip = pt['ip'].split('.')
        anon_ip = '{}.{}.0.0'.format(ip[0], ip[1])

        csv_fh.write(','.join(map(str, [
            epoch_ts, session_id, pt['expt_id'],
            anon_ip, pt['os'], pt['browser'], pt['screen_width'], pt['screen_height']
        ])) + '\n')

    csv_fh.close()


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
        global yaml_settings
        yaml_settings = yaml.safe_load(fh)

    # parse input dates
    start_time_str = args.start_date + 'T{}:00:00Z'.format(backup_hour)
    end_time_str = args.end_date + 'T{}:00:00Z'.format(backup_hour)

    for s_str, e_str in datetime_iter(start_time_str, end_time_str):
        dump_video_sent(s_str, e_str)
        dump_video_acked(s_str, e_str)
        dump_client_buffer(s_str, e_str)
        dump_client_sysinfo(s_str, e_str)


if __name__ == '__main__':
    main()
