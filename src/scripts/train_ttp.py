#!/usr/bin/env python3

import sys
import argparse
import yaml
import torch
import numpy as np

from helpers import connect_to_influxdb, try_parsing_time


VIDEO_DURATION = 180180
PAST_CHUNKS = 8
PKT_BYTES = 1500
MILLION = 1000000
BIN_SIZE = 0.5  # seconds
BIN_MAX = 20

# training related
BATCH_SIZE = 32
DIM_IN = 62
DIM_OUT = BIN_MAX + 1
# hidden dimensions
DIM_H = 100


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


def calculate_trans_times(video_sent_results, video_acked_results):
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
        dsv = d[session][video_ts]  # short name

        dsv['sent_ts'] = try_parsing_time(pt['time'])
        dsv['size'] = float(pt['size'])
        dsv['delivery_rate'] = float(pt['delivery_rate'])
        dsv['cwnd'] = float(pt['cwnd']) * PKT_BYTES
        dsv['in_flight'] = float(pt['in_flight']) * PKT_BYTES
        dsv['min_rtt'] = float(pt['min_rtt']) / MILLION
        dsv['rtt'] = float(pt['rtt']) / MILLION
        # dsv['ssim_index'] = get_ssim_index(pt)

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

        dsv = d[session][video_ts]  # short name

        # calculate transmission time
        sent_ts = dsv['sent_ts']
        acked_ts = try_parsing_time(pt['time'])
        dsv['acked_ts'] = acked_ts
        dsv['trans_time'] = (acked_ts - sent_ts).total_seconds()

    return d


# normalize a 2d numpy array
def normalize(x):
    # zero-centered
    y = np.array(x) - np.mean(x, axis=0)

    # normalized
    norms = np.std(y, axis=0)
    for col in range(len(norms)):
        if norms[col] != 0:
            y[:, col] /= norms[col]

    return y


# discretize a 1d numpy array, and clamp into [0, BIN_MAX]
def discretize(x):
    y = np.floor(np.array(x) / BIN_SIZE).astype(int)
    y = np.clip(y, 0, BIN_MAX)

    bin_sizes = np.zeros(BIN_MAX + 1, dtype=int)
    for bin_id in y:
        bin_sizes[bin_id] += 1
    print('output distribution:', bin_sizes)

    return y


def preprocess(d):
    x = []
    y = []

    for session in d:
        ds = d[session]
        for video_ts in ds:
            dsv = ds[video_ts]
            if 'trans_time' not in dsv:
                continue

            # construct a single row of input data
            row = []

            for i in reversed(range(1, 1 + PAST_CHUNKS)):
                ts = video_ts - i * VIDEO_DURATION
                if ts in ds and 'trans_time' in ds[ts]:
                    row += [ds[ts]['size'], ds[ts]['delivery_rate'],
                            ds[ts]['cwnd'], ds[ts]['in_flight'],
                            ds[ts]['min_rtt'], ds[ts]['rtt'],
                            ds[ts]['trans_time']]
                else:
                    row += [0, 0, 0, 0, 0, 0, 0]

            row += [dsv['size'], dsv['delivery_rate'],
                    dsv['cwnd'], dsv['in_flight'],
                    dsv['min_rtt'], dsv['rtt']]

            assert(len(row) == DIM_IN)
            x.append(row)
            y.append(dsv['trans_time'])

    return normalize(x), discretize(y)


class Model:
    def __init__(self):
        # define model, loss function, and optimizer
        self.model = torch.nn.Sequential(
            torch.nn.Linear(DIM_IN, DIM_H),
            torch.nn.ReLU(),
            torch.nn.Linear(DIM_H, DIM_OUT),
        ).double()
        self.loss_fn = torch.nn.CrossEntropyLoss()
        self.optimizer = torch.optim.Adam(self.model.parameters(),
                                          lr=1e-4, weight_decay=1e-3)

    # perform one step of training (forward + backward + optimize)
    def train_step(self, input_data, output_data):
        x = torch.from_numpy(input_data)
        y = torch.from_numpy(output_data)

        # forward pass
        y_scores = self.model(x)
        loss = self.loss_fn(y_scores, y)

        # backpropagation and optimize
        self.optimizer.zero_grad()
        loss.backward()
        self.optimizer.step()

        return loss.item()

    # compute loss
    def compute_loss(self, input_data, output_data):
        with torch.no_grad():
            x = torch.from_numpy(input_data)
            y = torch.from_numpy(output_data)

            y_scores = self.model(x)
            loss = self.loss_fn(y_scores, y)

            return loss.item()

    # compute accuracy of the classifier
    def compute_accuracy(self, input_data, output_data):
        correct = 0
        total = 0

        with torch.no_grad():
            x = torch.from_numpy(input_data)
            y = torch.from_numpy(output_data)

            y_scores = self.model(x)
            y_predicted = torch.max(y_scores, 1)[1]

            total += y.size(0)
            correct += (y_predicted == y).sum().item()

        return correct / total

    def save(self, model_path):
        torch.save(self.model.state_dict(), model_path)

    def load(self, model_path):
        self.model.load_state_dict(torch.load(model_path))
        self.model.eval()


def train(model, input_data, output_data):
    # split training data into training/validation
    num_training = int(0.8 * len(input_data))
    training_input = input_data[:num_training]
    training_output = output_data[:num_training]
    validation_input = input_data[num_training:]
    validation_output = output_data[num_training:]
    print('Training set size:', len(training_input))
    print('Validation set size:', len(validation_input))

    # loop over the entire dataset multiple times
    num_batches = int(np.ceil(num_training / BATCH_SIZE))
    for epoch_id in range(100):
        # permutate data in each epoch
        perm_indices = np.random.permutation(range(num_training))

        for batch_id in range(num_batches):
            start = batch_id * BATCH_SIZE
            end = min(start + BATCH_SIZE, num_training)
            batch_indices = perm_indices[start:end]

            # get a batch of input data
            batch_input = input_data[batch_indices]
            batch_output = output_data[batch_indices]

            model.train_step(batch_input, batch_output)

        print('epoch {:d}:\n'
              '  training loss {:.3f}, validation loss {:.3f}\n'
              '  training accuracy {:.3f}, validation accuracy {:.3f}'.format(
              epoch_id + 1,
              model.compute_loss(training_input, training_output),
              model.compute_loss(validation_input, validation_output),
              model.compute_accuracy(training_input, training_output),
              model.compute_accuracy(validation_input, validation_output)))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('yaml_settings')
    parser.add_argument('--from', dest='time_start', help='e.g., 12h, 2d (ago)')
    parser.add_argument('--to', dest='time_end', help='e.g., 6h, 1d (ago)')
    parser.add_argument('--load', help='model to load from')
    parser.add_argument('--save', help='model to save to', required=True)
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
    raw_data = calculate_trans_times(video_sent_results, video_acked_results)

    # preprocess data
    input_data, output_data = preprocess(raw_data)

    # create a model to train
    model = Model()
    if args.load:
        model.load(args.load)
        sys.stderr.write('Loaded model from {}\n'.format(args.load))

    # train a neural network with data
    train(model, input_data, output_data)

    model.save(args.save)
    sys.stderr.write('Saved model to {}\n'.format(args.save))


if __name__ == '__main__':
    main()
