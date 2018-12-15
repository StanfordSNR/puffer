#!/usr/bin/env python3

import os
import sys
import argparse
import yaml
from datetime import datetime
import numpy as np
from influxdb import InfluxDBClient
import psycopg2
import matplotlib
import matplotlib.pyplot as plt


# cache of Postgres data: experiment 'id' -> json 'data' of the experiment
expt_id_cache = {}


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


def retrieve_expt_config(postgres_cursor, expt_id):
    if expt_id not in expt_id_cache:
        postgres_cursor.execute(
            'SELECT * FROM puffer_experiment WHERE id={};'.format(expt_id))
        rows = postgres_cursor.fetchall()
        if len(rows) != 1:
            sys.exit('Error: invalid experiment ID {}'.format(expt_id))

        expt_id_cache[expt_id] = rows[0][2]

    return expt_id_cache[expt_id]


def collect_ssim(client_video_result, postgres_cursor):
    # process InfluxDB data
    x = {}
    for pt in client_video_result['client_video']:
        # only interested in video received by clients
        if pt['event'] != 'ack':
            continue

        expt_id = pt['expt_id']
        expt_config = retrieve_expt_config(postgres_cursor, expt_id)
        # index x by (abr, cc)
        abr_cc = (expt_config['abr'], expt_config['cc'])
        if abr_cc not in x:
            x[abr_cc] = []

        ssim_db = float(pt['ssim'])
        raw_ssim = 1 - 10 ** (ssim_db / -10)
        x[abr_cc].append(raw_ssim)

    # calculate average SSIM
    ssim = {}
    for abr_cc in x:
        avg_raw_ssim = np.mean(x[abr_cc])
        avg_ssim = -10 * np.log10(1 - avg_raw_ssim)
        ssim[abr_cc] = avg_ssim

    return ssim


def collect_rebuffer(client_buffer_result, postgres_cursor):
    # process InfluxDB data
    time_str = "%Y-%m-%dT%H:%M:%S.%fZ"
    x = {}
    for pt in client_buffer_result['client_buffer']:
        expt_id = pt['expt_id']
        expt_config = retrieve_expt_config(postgres_cursor, expt_id)
        # index x by (abr, cc)
        abr_cc = (expt_config['abr'], expt_config['cc'])
        if abr_cc not in x:
            x[abr_cc] = {}

        session = (pt['user'], pt['init_id'])
        if session not in x[abr_cc]:
            x[abr_cc][session] = {}
            x[abr_cc][session]['min_time'] = None
            x[abr_cc][session]['max_time'] = None
            x[abr_cc][session]['min_cum_rebuf'] = None
            x[abr_cc][session]['max_cum_rebuf'] = None

        # shorthand variable
        y = x[abr_cc][session]
        ts = datetime.strptime(pt['time'], time_str)
        cum_rebuf = float(pt['cum_rebuf'])

        if y['min_time'] is None or ts < y['min_time']:
            y['min_time'] = ts
        if y['max_time'] is None or ts > y['max_time']:
            y['max_time'] = ts

        if y['min_cum_rebuf'] is None or cum_rebuf < y['min_cum_rebuf']:
            y['min_cum_rebuf'] = cum_rebuf
        if y['max_cum_rebuf'] is None or cum_rebuf > y['max_cum_rebuf']:
            y['max_cum_rebuf'] = cum_rebuf

    # calculate rebuffer rate
    rebuffer = {}
    for abr_cc in x:
        total_play = 0
        total_rebuf = 0

        for session in x[abr_cc]:
            # shorthand variable
            y = x[abr_cc][session]
            total_play += (y['max_time'] - y['min_time']).total_seconds()
            total_rebuf += y['max_cum_rebuf'] - y['min_cum_rebuf']

        if total_play == 0:
            sys.exit('Error: total play time is 0')

        rebuf_rate = total_rebuf / total_play
        rebuffer[abr_cc] = rebuf_rate * 100

    return rebuffer


def plot_ssim_rebuffer(ssim, rebuffer, curr_ts, hours):
    curr_ts_str = curr_ts.strftime("%Y-%m-%dT%H:%M")

    fig, ax = plt.subplots()
    ax.set_title('Performance in the last {}h of {} (UTC)'
                 .format(hours, curr_ts_str))
    ax.set_xlabel('Rebuffer rate (%)')
    ax.set_ylabel('Average SSIM (dB)')
    ax.grid()

    for abr_cc in ssim:
        abr_cc_str = '{}+{}'.format(*abr_cc)
        if abr_cc not in rebuffer:
            sys.exit('Error: {} does not exist both ssim and rebuffer'
                     .format(abr_cc_str))

        x = ssim[abr_cc]
        y = rebuffer[abr_cc]
        ax.scatter(x, y)
        ax.annotate(abr_cc_str, (x, y))

    output_file = curr_ts_str + '.png'
    fig.savefig(output_file, dpi=300, bbox_inches='tight', pad_inches=0.2)
    sys.stderr.write('Saved plot to {}\n'.format(output_file))


def main():
    parser = argparse.ArgumentParser(
        'Run this script every hour to plot SSIM vs rebuffer rate')
    parser.add_argument('yaml_settings')
    parser.add_argument('--hours', type=int, default=1)
    args = parser.parse_args()
    hours = args.hours

    with open(args.yaml_settings, 'r') as fh:
        yaml_settings = yaml.safe_load(fh)

    curr_ts = datetime.utcnow()

    # create an InfluxDB client and perform queries
    influx_client = connect_to_influxdb(yaml_settings)

    # query client_video and client_buffer
    client_video_result = influx_client.query(
        'SELECT * FROM client_video WHERE time >= now() - {}h'.format(hours))
    client_buffer_result = influx_client.query(
        'SELECT * FROM client_buffer WHERE time >= now() - {}h'.format(hours))

    # create a Postgres client and perform queries
    postgres_client = connect_to_postgres(yaml_settings)
    postgres_cursor = postgres_client.cursor()

    # collect ssim and rebuffer
    ssim = collect_ssim(client_video_result, postgres_cursor)
    rebuffer = collect_rebuffer(client_buffer_result, postgres_cursor)

    if not ssim or not rebuffer:
        sys.exit('Error: no data found in the last {} hours'.format(hours))

    # plot ssim vs rebuffer
    plot_ssim_rebuffer(ssim, rebuffer, curr_ts, hours)

    postgres_cursor.close()


if __name__ == '__main__':
    main()
