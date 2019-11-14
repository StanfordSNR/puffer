#!/usr/bin/env python3

import argparse
import yaml
import struct
import numpy as np

import pyasn
import telnetlib

import hashlib
import base64

from helpers import datetime_iter, connect_to_influxdb, query_measurement


backup_hour = 11  # back up at 11 AM (UTC) every day

yaml_settings = None
session_map = {}  # { stream_key: session_id }

asndb = None
ip_asn_cache = {}  # { ip: ASN }
asn_users = {}  # { asn: set of users }


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


def find_asn(ip):
    global ip_asn_cache

    asn = asndb.lookup(ip)[0]
    if asn is not None:
        return asn

    if ip in ip_asn_cache:
        return ip_asn_cache[ip]

    # query RADb and parse the reply for ASN
    tn = telnetlib.Telnet('whois.radb.net', 43)
    tn.write(ip.encode('ascii') + b'\n')

    reply = tn.read_all().decode().split('\n')
    for entry in reply:
        items = entry.split(':')
        if items[0] == 'origin':
            asn = items[1].strip()
            if asn[0:2] == 'AS':
                asn = asn[2:]
            asn = int(asn)
            ip_asn_cache[ip] = asn  # add to cache
            break

    tn.close()
    return asn


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

        csv_fh.write(','.join(map(str, [
            epoch_ts, session_id, pt['expt_id'], '',
            pt['os'], pt['browser'], pt['screen_width'], pt['screen_height']
        ])) + '\n')

    csv_fh.close()


def build_asn_users(s_str, e_str):
    global asn_users

    # connect to InfluxDB
    influx_client = connect_to_influxdb(yaml_settings)

    client_sysinfo_results = query_measurement(
        influx_client, 'client_sysinfo', s_str, e_str)['client_sysinfo']

    for pt in client_sysinfo_results:
        user = pt['user']
        asn = find_asn(pt['ip'])

        if asn not in asn_users:
            asn_users[asn] = set()
        asn_users[asn].add(user)


def dump_client_sysinfo_asn(s_str, e_str):
    # connect to InfluxDB
    influx_client = connect_to_influxdb(yaml_settings)

    csv_fname = 'client_sysinfo_asn_{}.csv'.format(s_str[:-7])
    csv_fh = open(csv_fname, 'w')

    client_sysinfo_results = query_measurement(
        influx_client, 'client_sysinfo', s_str, e_str)['client_sysinfo']

    for pt in client_sysinfo_results:
        epoch_ts = np.datetime64(pt['time']).astype('datetime64[ms]').astype('int')
        stream_key = (pt['user'], int(pt['init_id']), int(pt['expt_id']))
        session_id = find_session_id(stream_key)

        asn = find_asn(pt['ip'])
        if len(asn_users[asn]) < 3:
            asn = 'anon'

        csv_fh.write(','.join(map(str, [
            epoch_ts, session_id, pt['expt_id'], asn,
            pt['os'], pt['browser'], pt['screen_width'], pt['screen_height']
        ])) + '\n')

    csv_fh.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('yaml_settings')
    parser.add_argument('--from', required=True, dest='start_date',
                        help='e.g., "2019-04-03" ({} AM in UTC)'.format(backup_hour))
    parser.add_argument('--to', required=True, dest='end_date',
                        help='e.g., "2019-04-05" ({} AM in UTC)'.format(backup_hour))
    parser.add_argument('--asn-db',
                        help='IP-to-ASN database and dump client_sysinfo only')
    args = parser.parse_args()

    with open(args.yaml_settings, 'r') as fh:
        global yaml_settings
        yaml_settings = yaml.safe_load(fh)

    # parse input dates
    start_time_str = args.start_date + 'T{}:00:00Z'.format(backup_hour)
    end_time_str = args.end_date + 'T{}:00:00Z'.format(backup_hour)

    if args.asn_db:
        global asndb
        asndb = pyasn.pyasn(args.asn_db)

        # build asn_users for anonymization
        for s_str, e_str in datetime_iter(start_time_str, end_time_str):
            build_asn_users(s_str, e_str)

    for s_str, e_str in datetime_iter(start_time_str, end_time_str):
        if args.asn_db:
            dump_client_sysinfo_asn(s_str, e_str)
        else:
            dump_video_sent(s_str, e_str)
            dump_video_acked(s_str, e_str)
            dump_client_buffer(s_str, e_str)
            dump_client_sysinfo(s_str, e_str)


if __name__ == '__main__':
    main()
