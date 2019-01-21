import os
import sys
import errno
import yaml
import subprocess
import psycopg2
import numpy as np
from datetime import datetime
from influxdb import InfluxDBClient


VIDEO_DURATION = 180180
PKT_BYTES = 1500
MILLION = 1000000


def print_cmd(cmd):
    if isinstance(cmd, list):
        cmd_to_print = ' '.join(cmd).strip()
    else:
        cmd_to_print = cmd.strip()

    sys.stderr.write('$ {}\n'.format(cmd_to_print))


def call(cmd, **kwargs):
    print_cmd(cmd)
    return subprocess.call(cmd, **kwargs)


def check_call(cmd, **kwargs):
    print_cmd(cmd)
    return subprocess.check_call(cmd, **kwargs)


def check_output(cmd, **kwargs):
    print_cmd(cmd)
    return subprocess.check_output(cmd, **kwargs)


def Popen(cmd, **kwargs):
    print_cmd(cmd)
    return subprocess.Popen(cmd, **kwargs)


def make_sure_path_exists(target_path):
    try:
        os.makedirs(target_path)
    except OSError as exception:
        if exception.errno != errno.EEXIST:
            raise


def connect_to_influxdb(yaml_settings):
    influx = yaml_settings['influxdb_connection']
    influx_client = InfluxDBClient(
        influx['host'], influx['port'], influx['user'],
        os.environ[influx['password']], influx['dbname'])
    sys.stderr.write('Connected to the InfluxDB at {}:{}\n'
                     .format(influx['host'], influx['port']))
    return influx_client


def connect_to_postgres(yaml_settings):
    postgres = yaml_settings['postgres_connection']

    kwargs = {
        'host': postgres['host'],
        'port': postgres['port'],
        'database': postgres['dbname'],
        'user': postgres['user'],
        'password': os.environ[postgres['password']]
    }

    if 'sslmode' in postgres:
        kwargs['sslmode'] = postgres['sslmode']
        kwargs['sslrootcert'] = postgres['sslrootcert']
        kwargs['sslcert'] = postgres['sslcert']
        kwargs['sslkey'] = postgres['sslkey']

    postgres_client = psycopg2.connect(**kwargs)
    sys.stderr.write('Connected to the PostgreSQL at {}:{}\n'
                     .format(postgres['host'], postgres['port']))
    return postgres_client


def try_parsing_time(timestamp):
    time_fmts = ['%Y-%m-%dT%H:%M:%S.%fZ', '%Y-%m-%dT%H:%M:%SZ']

    for fmt in time_fmts:
        try:
            return datetime.strptime(timestamp, fmt)
        except ValueError:
            pass

    raise ValueError('No valid format found to parse ' + timestamp)


def ssim_db_to_index(ssim_db):
    return 1 - 10 ** (ssim_db / -10)


def ssim_index_to_db(ssim_index):
    return -10 * np.log10(1 - ssim_index)


# retrieve the config of expt_id: find in expt_id_cache if exists;
# otherwise, query Postgres and save the returned config in expt_id_cache
def retrieve_expt_config(expt_id, expt_id_cache, postgres_cursor):
    if expt_id not in expt_id_cache:
        postgres_cursor.execute(
            'SELECT * FROM puffer_experiment WHERE id={};'.format(expt_id))
        rows = postgres_cursor.fetchall()
        if len(rows) != 1:
            sys.exit('Error: invalid experiment ID {}'.format(expt_id))

        expt_id_cache[expt_id] = rows[0][2]

    return expt_id_cache[expt_id]


def create_time_clause(time_start, time_end):
    time_clause = None

    if time_start is not None:
        time_clause = "time >= '{}'".format(time_start)
    if time_end is not None:
        if time_clause is None:
            time_clause = "time <= '{}'".format(time_end)
        else:
            time_clause += " AND time <= '{}'".format(time_end)

    return time_clause


def get_ssim_index(pt):
    if 'ssim_index' in pt and pt['ssim_index'] is not None:
        return float(pt['ssim_index'])

    if 'ssim' in pt and pt['ssim'] is not None:
        return ssim_db_to_index(float(pt['ssim']))

    return None


def calculate_trans_times(video_sent_results, video_acked_results,
                          cc, postgres_cursor):
    d = {}
    last_video_ts = {}

    # cache of Postgres data: experiment 'id' -> json 'data' of the experiment
    expt_id_cache = {}

    for pt in video_sent_results['video_sent']:
        expt_id = int(pt['expt_id'])
        session = (pt['user'], int(pt['init_id']),
                   pt['channel'], expt_id)

        # filter data points by congestion control
        expt_config = retrieve_expt_config(expt_id, expt_id_cache,
                                           postgres_cursor)
        if cc is not None and expt_config['cc'] != cc:
            continue

        if session not in d:
            d[session] = {}
            last_video_ts[session] = None

        video_ts = int(pt['video_ts'])

        if last_video_ts[session] is not None:
            if video_ts != last_video_ts[session] + VIDEO_DURATION:
                sys.stderr.write('Warning in session {}: video_ts={}\n'
                                 .format(session, video_ts))
                continue

        last_video_ts[session] = video_ts

        d[session][video_ts] = {}
        dsv = d[session][video_ts]  # short name

        dsv['sent_ts'] = try_parsing_time(pt['time'])  # datetime object
        dsv['size'] = float(pt['size'])  # bytes
        dsv['delivery_rate'] = float(pt['delivery_rate'])  # byte/second
        dsv['cwnd'] = float(pt['cwnd'])  # packets
        dsv['in_flight'] = float(pt['in_flight'])  # packets
        dsv['min_rtt'] = float(pt['min_rtt']) / MILLION  # us -> s
        dsv['rtt'] = float(pt['rtt']) / MILLION  # us -> s
        dsv['ssim_index'] = get_ssim_index(pt)  # unitless (not in dB)

    for pt in video_acked_results['video_acked']:
        expt_id = int(pt['expt_id'])
        session = (pt['user'], int(pt['init_id']),
                   pt['channel'], expt_id)

        # filter data points by congestion control
        expt_config = retrieve_expt_config(expt_id, expt_id_cache,
                                           postgres_cursor)
        if cc is not None and expt_config['cc'] != cc:
            continue

        if session not in d:
            sys.stderr.write('Warning: ignored session {}\n'.format(session))
            continue

        video_ts = int(pt['video_ts'])
        if video_ts not in d[session]:
            sys.stderr.write('Warning: ignored acked video_ts {} in the '
                             'session {}\n'.format(video_ts, session))
            continue

        dsv = d[session][video_ts]  # short name

        # calculate transmission time and throughput
        sent_ts = dsv['sent_ts']
        acked_ts = try_parsing_time(pt['time'])  # datetime object
        dsv['acked_ts'] = acked_ts
        dsv['trans_time'] = (acked_ts - sent_ts).total_seconds()  # seconds
        dsv['throughput'] = dsv['size'] / dsv['trans_time']  # byte/second

    return d


def prepare_raw_data(yaml_settings_path, time_start, time_end, cc):
    with open(yaml_settings_path, 'r') as fh:
        yaml_settings = yaml.safe_load(fh)

    # construct time clause after 'WHERE'
    time_clause = create_time_clause(time_start, time_end)

    # create a client connected to InfluxDB
    influx_client = connect_to_influxdb(yaml_settings)

    # perform queries in InfluxDB
    video_sent_query = 'SELECT * FROM video_sent'
    if time_clause is not None:
        video_sent_query += ' WHERE ' + time_clause
    video_sent_results = influx_client.query(video_sent_query)
    if not video_sent_results:
        sys.exit('Error: no results returned from query: ' + video_sent_query)

    video_acked_query = 'SELECT * FROM video_acked'
    if time_clause is not None:
        video_acked_query += ' WHERE ' + time_clause
    video_acked_results = influx_client.query(video_acked_query)
    if not video_acked_results:
        sys.exit('Error: no results returned from query: ' + video_acked_query)

    # create a client connected to Postgres
    postgres_client = connect_to_postgres(yaml_settings)
    postgres_cursor = postgres_client.cursor()

    # calculate chunk transmission times
    ret = calculate_trans_times(video_sent_results, video_acked_results,
                                cc, postgres_cursor)

    postgres_cursor.close()
    return ret
