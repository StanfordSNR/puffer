import os
import sys
from influxdb import InfluxDBClient
import psycopg2


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
