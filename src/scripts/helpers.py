import os
from os import path
import sys
import errno
import yaml
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


def filter_video_data_by_cc(d, yaml_settings, required_cc):
    # cache of Postgres data: experiment 'id' -> json 'data' of the experiment
    expt_id_cache = {}

    # create a client connected to Postgres
    postgres_client = connect_to_postgres(yaml_settings)
    postgres_cursor = postgres_client.cursor()

    sessions_to_remove = []
    for session in d:
        expt_id = session[-1]
        expt_config = retrieve_expt_config(expt_id, expt_id_cache,
                                           postgres_cursor)
        cc = expt_config['cc']
        if cc != required_cc:
            sessions_to_remove.append(session)

    for session in sessions_to_remove:
        del d[session]

    postgres_cursor.close()


abr_order = ['puffer_ttp_cl', 'puffer_ttp_static', 'puffer_ttp_emu',
             'linear_bba', 'mpc', 'robust_mpc', 'pensieve']

pretty_names = {
    'bbr': 'BBR',
    'cubic': 'Cubic',
    'puffer_ttp_cl': 'Fugu',
    'puffer_ttp_static': 'Non-continual Fugu',
    'puffer_ttp_init': 'Non-continual Fugu',
    'puffer_ttp': 'Fugu',
    'puffer_ttp_emu': 'Emulation-trained Fugu',
    'linear_bba': 'Buffer-based',
    'mpc': 'MPC',
    'robust_mpc': 'RobustMPC',
    'pensieve': 'Pensieve',
}


pretty_colors = {
  'puffer_ttp_cl': '#d62728',
  'puffer_ttp': '#d62778',
  'puffer_ttp_static': '#ff7f0e',
  'puffer_ttp_init': '#ff7f0e',
  'puffer_ttp_emu': '#e377c2',
  'linear_bba': '#2ca02c',
  'mpc': '#1f77b4',
  'robust_mpc': '#8c564b',
  'pensieve': '#17becf',
}

pretty_styles = {
    'puffer_ttp_cl': (0, ()),
    'puffer_ttp': (0, ()),
    'puffer_ttp_static': (0, (3, 1, 1, 1)),
    'puffer_ttp_init': (0, (3, 1, 1, 1)),
    'puffer_ttp_emu': (0, (1, 3)),
    'linear_bba': (0, (3, 5, 1, 5)),
    'mpc': (0, (1,1)),
    'robust_mpc': (0, (3,3)),
    'pensieve': (0, (4,1)),
}
