#!/usr/bin/env python3

import argparse
import yaml
import torch

from helpers import connect_to_influxdb


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

    # TODO: calculate chunk transmission times by iterating over
    # video_sent_results and video_acked_results

    # TODO: training
    # train()

if __name__ == '__main__':
    main()
