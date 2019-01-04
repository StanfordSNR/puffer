#!/usr/bin/env python3

import argparse
import yaml
import torch

from helpers import connect_to_influxdb, try_parsing_time


def train():
    N = 32
    D_in, H, D_out = 5, 4, 3

    x = torch.randn(N, D_in)
    y = torch.randn(N, D_out)

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


def process_data(video_sent_results, video_acked_results):
    # create shorthand var names
    d = {}

    for pt in video_sent_results['video_sent']:
        # TODO: (user, init_id) might not be unique
        session = (pt['user'], int(pt['init_id']))
        if session not in d:
            d[session] = {}

        video_ts = int(pt['video_ts'])
        if video_ts in d[session]:
            sys.exit('same video_ts {} is sent twice in the session {}'
                     .format(video_ts, session))

        d[session][video_ts] = {}
        d[session][video_ts]['cwnd'] = int(pt['cwnd'])
        d[session][video_ts]['delivery_rate'] = int(pt['delivery_rate'])
        d[session][video_ts]['in_flight'] = int(pt['in_flight'])
        d[session][video_ts]['min_rtt'] = int(pt['min_rtt'])
        d[session][video_ts]['rtt'] = int(pt['rtt'])
        d[session][video_ts]['size'] = int(pt['size'])

        # calculate transmission time: save the sent timestamp first
        d[session][video_ts]['trans_time'] = try_parsing_time(pt['time'])

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

        # calculate transmission time: replace trans_time with correct value
        sent_ts = d[session][video_ts]['trans_time']
        acked_ts = try_parsing_time(pt['time'])
        d[session][video_ts]['trans_time'] = (acked_ts - sent_ts).total_seconds()

    return d


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('yaml_settings')
    parser.add_argument('-s', '--start-days-ago',
                        type=int, help='>= now() - {}d')
    parser.add_argument('-e', '--end-days-ago',
                        type=int, help='<= now() - {}d')
    # TODO: load a previously trained model to perform daily training
    # parser.add_argument('-m', '--model', help='model to start training with')
    args = parser.parse_args()

    with open(args.yaml_settings, 'r') as fh:
        yaml_settings = yaml.safe_load(fh)

    # construct time clause after 'WHERE'
    time_clause = None
    if args.start_days_ago is not None:
        time_clause = 'time >= now() - {}d'.format(args.start_days_ago)
    if args.end_days_ago is not None:
        end_date_clause = 'time <= now() - {}d'.format(args.end_days_ago)
        if time_clause is None:
            time_clause = end_date_clause
        else:
            time_clause += ' AND ' + end_date_clause

    influx_client = connect_to_influxdb(yaml_settings)

    # perform queries in InfluxDB
    video_sent_query = 'SELECT * FROM video_sent'
    if time_clause is not None:
        video_sent_query += ' WHERE ' + time_clause
    video_sent_results = influx_client.query(video_sent_query)

    video_acked_query = 'SELECT * FROM video_acked'
    if time_clause is not None:
        video_acked_query += ' WHERE ' + time_clause
    video_acked_results = influx_client.query(video_acked_query)

    # calculate chunk transmission times by iterating over
    # video_sent_results and video_acked_results
    data = process_data(video_sent_results, video_acked_results)

    # TODO: training
    # train()

if __name__ == '__main__':
    main()
