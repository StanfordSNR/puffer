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


def gen_session_id(stream_key):
    m = hashlib.sha256()
    m.update(stream_key[0].encode('utf-8'))
    m.update(struct.pack('>I', stream_key[1]))
    m.update(struct.pack('>I', stream_key[2]))
    return base64.b64encode(m.digest()).decode('ascii')


def parse_video_sent(s_str, e_str, d):
    # connect to InfluxDB
    influx_client = connect_to_influxdb(yaml_settings)
    video_sent_results = query_measurement(
        influx_client, 'video_sent', s_str, e_str)['video_sent']

    for pt in video_sent_results:
        stream_key = (pt['user'], int(pt['init_id']), int(pt['expt_id']))
        if stream_key not in d:
            d[stream_key] = {}

        video_ts = int(pt['video_ts'])
        d[stream_key][video_ts] = {}
        dsv = d[stream_key][video_ts]  # short name

        dsv['sent_ts'] = np.datetime64(pt['time'])
        dsv['size'] = int(pt['size'])  # bytes
        dsv['min_rtt'] = '{:.1f}'.format(float(pt['min_rtt']) / 1000)  # us -> ms


def parse_video_acked(s_str, e_str, d):
    # connect to InfluxDB
    influx_client = connect_to_influxdb(yaml_settings)
    video_acked_results = query_measurement(
        influx_client, 'video_acked', s_str, e_str)['video_acked']

    csv_fname = 'raw_trace_{}.csv'.format(s_str[:-7])
    csv_fh = open(csv_fname, 'w')

    for pt in video_acked_results:
        stream_key = (pt['user'], int(pt['init_id']), int(pt['expt_id']))
        if stream_key not in d:
            continue

        video_ts = int(pt['video_ts'])
        if video_ts not in d[stream_key]:
            continue
        dsv = d[stream_key][video_ts]  # short name

        acked_ts = np.datetime64(pt['time'])
        trans_time = '{:.1f}'.format(
            (acked_ts - dsv['sent_ts']) / np.timedelta64(1, 'ms'))  # ms

        epoch_ts = dsv['sent_ts'].astype('datetime64[ms]').astype('int')  # ms
        session_id = gen_session_id(stream_key)

        csv_fh.write(','.join(map(str, [
            epoch_ts, session_id, dsv['size'], trans_time, dsv['min_rtt'],
            float(pt['cum_rebuffer']) * 1000
        ])) + '\n')

    csv_fh.close()


def dump_raw_trace(s_str, e_str):
    d = {}
    parse_video_sent(s_str, e_str, d)
    parse_video_acked(s_str, e_str, d)


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
        dump_raw_trace(s_str, e_str)


if __name__ == '__main__':
    main()
