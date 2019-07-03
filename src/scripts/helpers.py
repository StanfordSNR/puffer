import os
from os import path
import sys
import errno
import subprocess
import psycopg2
import numpy as np
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
            sys.stderr.write('Error: invalid experiment ID {}\n'.format(expt_id))
            return None

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


def query_measurement(influx_client, measurement, time_start, time_end):
    time_clause = create_time_clause(time_start, time_end)

    query = 'SELECT * FROM ' + measurement
    if time_clause is not None:
        query += ' WHERE ' + time_clause

    results = influx_client.query(query)
    if not results:
        sys.exit('Error: no results returned from query: ' + query)

    return results


def get_abr_cc(expt_config):
    if 'abr_name' in expt_config:
        abr_cc = (expt_config['abr_name'], expt_config['cc'])
    else:
        abr = expt_config['abr']

        if 'puffer_ttp' in abr:
            model_dir = path.basename(expt_config['abr_config']['model_dir'])

            if 'bbr-2019' in model_dir or 'cubic-2019' in model_dir:
                abr = 'puffer_ttp_cl'
            else:
                abr = 'puffer_ttp_static'
        abr_cc = (abr, expt_config['cc'])

    return abr_cc


def get_expt_id(pt):
    if pt['expt_id'] is not None:
        return int(pt['expt_id'])
    elif pt['expt_id_1'] is not None:
        return int(pt['expt_id_1'])

    return None


def get_user(pt):
    if pt['user'] is not None:
        return pt['user']
    elif pt['user_1'] is not None:
        return pt['user_1']

    return None
