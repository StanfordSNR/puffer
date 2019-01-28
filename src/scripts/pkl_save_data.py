#!/usr/bin/env python3

import sys
import time
import yaml
import pickle
import argparse

from helpers import connect_to_influxdb, query_measurement
from collect_data import video_data_by_session, buffer_data_by_session


def collect_video_data(influx_client, args):
    t1 = time.time()

    video_sent_results = query_measurement(influx_client, 'video_sent',
                                           args.time_start, args.time_end)
    video_acked_results = query_measurement(influx_client, 'video_acked',
                                            args.time_start, args.time_end)

    t2 = time.time()
    sys.stderr.write('Queried video_sent & video_acked in InfluxDB: {:.1f} min\n'
                     .format((t2 - t1) / 60.0))

    video_data = video_data_by_session(video_sent_results, video_acked_results)

    t3 = time.time()
    sys.stderr.write('Processed and created video_data: {:.1f} min\n'
                     .format((t3 - t2) / 60.0))

    return video_data


def collect_buffer_data(influx_client, args):
    t1 = time.time()

    client_buffer_results = query_measurement(influx_client, 'client_buffer',
                                              args.time_start, args.time_end)
    t2 = time.time()
    sys.stderr.write('Queried client_buffer in InfluxDB: {:.1f} min\n'
                     .format((t2 - t1) / 60.0))

    buffer_data = buffer_data_by_session(client_buffer_results)

    t3 = time.time()
    sys.stderr.write('Processed and created buffer_data: {:.1f} min\n'
                     .format((t3 - t2) / 60.0))

    return buffer_data


def save_to_pickle(video_data, buffer_data, args):
    mutual_sessions = video_data.keys() & buffer_data.keys()
    sys.stderr.write('Mutual session count in video_data & buffer_data: {}\n'
                     .format(len(mutual_sessions)))

    t1 = time.time()

    # save video_data
    if args.time_start and args.time_end:
        video_data_file_name = 'video_data_{}_{}.pickle'.format(
            args.time_start, args.time_end)
    else:
        video_data_file_name = 'video_data.pickle'

    with open(video_data_file_name, 'wb') as fh:
        pickle.dump(video_data, fh, protocol=pickle.HIGHEST_PROTOCOL)

    t2 = time.time()
    sys.stderr.write('Saved video_data to {}: {:.1f} min\n'
                     .format(video_data_file_name, (t2 - t1) / 60.0))

    # save buffer_data
    if args.time_start and args.time_end:
        buffer_data_file_name = 'buffer_data_{}_{}.pickle'.format(
            args.time_start, args.time_end)
    else:
        buffer_data_file_name = 'buffer_data.pickle'

    with open(buffer_data_file_name, 'wb') as fh:
        pickle.dump(buffer_data, fh, protocol=pickle.HIGHEST_PROTOCOL)

    t3 = time.time()
    sys.stderr.write('Saved buffer_data to {}: {:.1f} min\n'
                     .format(buffer_data_file_name, (t3 - t2) / 60.0))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('yaml_settings')
    parser.add_argument('--from', dest='time_start',
                        help='datetime in UTC conforming to RFC3339')
    parser.add_argument('--to', dest='time_end',
                        help='datetime in UTC conforming to RFC3339')
    args = parser.parse_args()

    with open(args.yaml_settings, 'r') as fh:
        yaml_settings = yaml.safe_load(fh)

    # create an InfluxDB client and perform queries
    influx_client = connect_to_influxdb(yaml_settings)

    # collect ssim and rebuffer
    video_data = collect_video_data(influx_client, args)

    buffer_data = collect_buffer_data(influx_client, args)

    if not video_data or not buffer_data:
        sys.exit('Error: no data found in the queried range')

    # save to pickle files
    save_to_pickle(video_data, buffer_data, args)


if __name__ == '__main__':
    main()
