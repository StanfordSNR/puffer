import os
import sys
import errno
import subprocess
import psycopg2
import numpy as np
from datetime import datetime
from influxdb import InfluxDBClient


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


def get_ssim_index(pt):
    if 'ssim_index' in pt and pt['ssim_index'] is not None:
        return float(pt['ssim_index'])

    if 'ssim' in pt and pt['ssim'] is not None:
        return ssim_db_to_index(float(pt['ssim']))

    return None
