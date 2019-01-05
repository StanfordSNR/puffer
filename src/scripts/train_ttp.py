#!/usr/bin/env python3

import sys
import argparse
import yaml
import torch
import numpy as np

from helpers import connect_to_influxdb, try_parsing_time


VIDEO_DURATION = 180180
PAST_CHUNKS = 4
PKT_BYTES = 1500
MILLION = 1000000


def create_time_clause(time_start, time_end):
    time_clause = None

    if time_start is not None:
        time_clause = 'time >= now()-' + time_start
    if time_end is not None:
        if time_clause is None:
            time_clause = 'time <= now()-' + time_end
        else:
            time_clause += ' AND time <= now()-' + time_end

    return time_clause


def get_ssim_index(pt):
    if 'ssim_index' in pt and pt['ssim_index'] is not None:
        return float(pt['ssim_index'])

    if 'ssim' in pt and pt['ssim'] is not None:
        return ssim_db_to_index(float(pt['ssim']))

    return None


def process_data(video_sent_results, video_acked_results):
    d = {}
    last_video_ts = {}

    for pt in video_sent_results['video_sent']:
        session = (pt['user'], int(pt['init_id']),
                   pt['channel'], int(pt['expt_id']))
        if session not in d:
            d[session] = {}
            last_video_ts[session] = None

        video_ts = int(pt['video_ts'])

        if last_video_ts[session] is not None:
            if video_ts != last_video_ts[session] + VIDEO_DURATION:
                sys.exit('Error in session {}: last_video_ts={}, video_ts={}'
                         .format(session, last_video_ts[session], video_ts))
        last_video_ts[session] = video_ts

        d[session][video_ts] = {}
        d[session][video_ts]['sent_ts'] = try_parsing_time(pt['time'])
        d[session][video_ts]['cwnd'] = int(pt['cwnd'])
        d[session][video_ts]['delivery_rate'] = int(pt['delivery_rate'])
        d[session][video_ts]['in_flight'] = int(pt['in_flight'])
        d[session][video_ts]['min_rtt'] = int(pt['min_rtt'])
        d[session][video_ts]['rtt'] = int(pt['rtt'])
        d[session][video_ts]['ssim_index'] = get_ssim_index(pt)
        d[session][video_ts]['size'] = int(pt['size'])

    for pt in video_acked_results['video_acked']:
        session = (pt['user'], int(pt['init_id']),
                   pt['channel'], int(pt['expt_id']))
        if session not in d:
            sys.stderr.write('Warning: ignored session {}\n'.format(session))
            continue

        video_ts = int(pt['video_ts'])
        if video_ts not in d[session]:
            sys.stderr.write('Warning: ignored acked video_ts {} in the '
                             'session {}\n'.format(video_ts, session))
            continue

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

            row += [dsv['size'], dsv['delivery_rate'],
                    dsv['cwnd'] * 1500, dsv['in_flight'] * 1500,
                    dsv['min_rtt'] / MILLION, dsv['rtt'] / MILLION]

            # TODO: normalize row

            # TODO: perform training when a batch is created
            # do_train()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('yaml_settings')
    parser.add_argument('--from', dest='time_start', help='e.g., 12h, 2d (ago)')
    parser.add_argument('--to', dest='time_end', help='e.g., 6h, 1d (ago)')
    # TODO: load a previously trained model to perform daily training
    # parser.add_argument('-m', '--model', help='model to start training with')
    args = parser.parse_args()

    with open(args.yaml_settings, 'r') as fh:
        yaml_settings = yaml.safe_load(fh)

    # construct time clause after 'WHERE'
    time_clause = create_time_clause(args.time_start, args.time_end)

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
