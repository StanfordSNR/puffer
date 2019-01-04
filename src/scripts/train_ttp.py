#!/usr/bin/env python3

import sys
import argparse
import yaml
import torch
import numpy as np

from helpers import connect_to_influxdb, try_parsing_time


VIDEO_DURATION = 180180
PAST_CHUNKS = 4


def create_time_clause(range_from, range_to):
    time_clause = None

    if range_from is not None:
        time_clause = 'time >= now()-' + range_from
    if range_to is not None:
        if time_clause is None:
            time_clause = 'time <= now()-' + range_to
        else:
            time_clause += ' AND time <= now()-' + range_to

    return time_clause


def process_data(video_sent_results, video_acked_results):
    d = {}

    for pt in video_sent_results['video_sent']:
        # TODO: (user, init_id) might be not unique
        session = (pt['user'], int(pt['init_id']))
        if session not in d:
            d[session] = {}

        video_ts = int(pt['video_ts'])
        if video_ts in d[session]:
            sys.exit('same video_ts {} is sent twice in the session {}'
                     .format(video_ts, session))

        d[session][video_ts] = {}
        d[session][video_ts]['sent_ts'] = try_parsing_time(pt['time'])
        d[session][video_ts]['cwnd'] = int(pt['cwnd'])
        d[session][video_ts]['delivery_rate'] = int(pt['delivery_rate'])
        d[session][video_ts]['in_flight'] = int(pt['in_flight'])
        d[session][video_ts]['min_rtt'] = int(pt['min_rtt'])
        d[session][video_ts]['rtt'] = int(pt['rtt'])
        d[session][video_ts]['size'] = int(pt['size'])

        if pt['ssim_index'] is not None:
            d[session][video_ts]['ssim_index'] = float(pt['ssim_index'])
        elif d['ssim'] is not None:
            ssim = float(pt['ssim'])
            ssim_index = 1 - 10 ** (ssim / -10)
            d[session][video_ts]['ssim_index'] = ssim_index
        else:
            sys.exit('fatal error: both ssim_index and ssim are missing')

    for pt in video_acked_results['video_acked']:
        session = (pt['user'], int(pt['init_id']))
        if session not in d:
            sys.stderr.write('ignored session {}\n'.format(session))

        video_ts = int(pt['video_ts'])
        if video_ts not in d[session]:
            sys.stderr.write('ignored acked video_ts {} in the session {}\n'
                             .format(video_ts, session))

        # calculate transmission time
        sent_ts = d[session][video_ts]['sent_ts']
        acked_ts = try_parsing_time(pt['time'])
        d[session][video_ts]['acked_ts'] = acked_ts
        d[session][video_ts]['trans_time'] = (acked_ts - sent_ts).total_seconds()

    return d


def do_train():
    N = 32  # batch size
    D_in, H, D_out = 5, 4, 3

    x = torch.zeros(N, D_in)
    y = torch.zeros(N, D_out)

    model = torch.nn.Sequential(
        torch.nn.Linear(D_in, H),
        torch.nn.ReLU(),
        torch.nn.Linear(H, D_out),
    )
    loss_fn = torch.nn.MSELoss()

    learning_rate = 1e-4
    optimizer = torch.optim.Adam(model.parameters(), lr=learning_rate)

    max_iters = 500
    for t in range(max_iters):
        y_pred = model(x)

        loss = loss_fn(y_pred, y)
        print(t, loss.item())

        optimizer.zero_grad()
        loss.backward()
        optimizer.step()


def train(d):
    for session in d:
        ds = d[session]
        for video_ts in ds:
            dsv = ds[video_ts]

            if 'trans_time' not in dsv:
                continue

            row = []

            for i in reversed(range(1, 1 + PAST_CHUNKS)):
                ts = video_ts - i * VIDEO_DURATION
                if ts in ds and 'trans_time' in ds[ts]:
                    row += [ds[ts]['size'], ds[ts]['trans_time']]
                else:
                    row += [0, 0]

            row += [dsv['size'], dsv['cwnd'], dsv['delivery_rate'],
                    dsv['in_flight'], dsv['min_rtt'], dsv['rtt']]
            # TODO: normalize row
            print(row, dsv['trans_time'])

            # TODO: perform training when a batch is created
            # do_train()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('yaml_settings')
    parser.add_argument('--from', dest='range_from', help='e.g., 12h, 2d (ago)')
    parser.add_argument('--to', dest='range_to', help='e.g., 6h, 1d (ago)')
    # TODO: load a previously trained model to perform daily training
    # parser.add_argument('-m', '--model', help='model to start training with')
    args = parser.parse_args()

    with open(args.yaml_settings, 'r') as fh:
        yaml_settings = yaml.safe_load(fh)

    # construct time clause after 'WHERE'
    time_clause = create_time_clause(args.range_from, args.range_to)

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

    # calculate chunk transmission times
    data = process_data(video_sent_results, video_acked_results)

    # train a neural network with data
    train(data)

if __name__ == '__main__':
    main()
