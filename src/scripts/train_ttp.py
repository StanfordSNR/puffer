#!/usr/bin/env python3

import sys
import argparse
import yaml
import torch
import numpy as np

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

from helpers import connect_to_influxdb, try_parsing_time


VIDEO_DURATION = 180180
PKT_BYTES = 1500
MILLION = 1000000

# training related
BATCH_SIZE = 64
PAST_CHUNKS = 8
FUTURE_CHUNKS = 5
DIM_IN = 67
BIN_SIZE = 0.5  # seconds
BIN_MAX = 20
DIM_OUT = BIN_MAX + 1
DIM_H1 = 50
DIM_H2 = 50
LEARNING_RATE = 1e-4
WEIGHT_DECAY = 1e-3
NUM_EPOCHS = 500

TUNING = False
DEVICE = torch.device('cpu')


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
        dsv['size'] = float(pt['size']) / PKT_BYTES  # bytes -> packets
        # byte/second -> packet/second
        dsv['delivery_rate'] = float(pt['delivery_rate']) / PKT_BYTES
        dsv['cwnd'] = float(pt['cwnd'])
        dsv['in_flight'] = float(pt['in_flight'])
        dsv['min_rtt'] = float(pt['min_rtt']) / MILLION  # us -> s
        dsv['rtt'] = float(pt['rtt']) / MILLION  # us -> s
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
    return np.clip(y, 0, BIN_MAX)


def one_hot(index, total):
    return [int(i == index) for i in range(total)]


def append_past_chunks(ds, next_ts, row):
    i = 1
    past_chunks = []

    while i <= PAST_CHUNKS:
        ts = next_ts - i * VIDEO_DURATION
        if ts in ds and 'trans_time' in ds[ts]:
            past_chunks = [ds[ts]['delivery_rate'],
                           ds[ts]['cwnd'], ds[ts]['in_flight'],
                           ds[ts]['min_rtt'], ds[ts]['rtt'],
                           ds[ts]['size'], ds[ts]['trans_time']] + past_chunks
        else:
            nts = ts + VIDEO_DURATION  # padding with the nearest ts
            padding = [ds[nts]['delivery_rate'],
                       ds[nts]['cwnd'], ds[nts]['in_flight'],
                       ds[nts]['min_rtt'], ds[nts]['rtt']]

            if nts == next_ts:
                padding += [0, 0]  # next_ts is the first chunk to send
            else:
                padding += [ds[nts]['size'], ds[nts]['trans_time']]

            break

        i += 1

    if i != PAST_CHUNKS + 1:  # break in the middle; padding must exist
        while i <= PAST_CHUNKS:
            past_chunks = padding + past_chunks
            i += 1

    row += past_chunks


def preprocess(d):
    x = []
    y = []

    for session in d:
        ds = d[session]

        for next_ts in ds:
            if 'trans_time' not in ds[next_ts]:
                continue

            # construct a single row of input data
            row = []

            # append past chunks with padding
            append_past_chunks(ds, next_ts, row)

            # append the TCP info of the next chunk
            row += [ds[next_ts]['delivery_rate'],
                    ds[next_ts]['cwnd'], ds[next_ts]['in_flight'],
                    ds[next_ts]['min_rtt'], ds[next_ts]['rtt']]

            # generate FUTURE_CHUNKS rows
            for i in range(FUTURE_CHUNKS):
                row_h = row.copy()

                ts = next_ts + i * VIDEO_DURATION
                if ts in ds and 'trans_time' in ds[ts]:
                    row_h += [ds[ts]['size']]
                    # one-hot encoding of 'i'
                    row_h += one_hot(i, FUTURE_CHUNKS)

                    assert(len(row_h) == DIM_IN)
                    x.append(row_h)
                    y.append(ds[ts]['trans_time'])

    x = normalize(x)
    y = discretize(y)

    # print label distribution
    bin_sizes = np.zeros(BIN_MAX + 1, dtype=int)
    for bin_id in y:
        bin_sizes[bin_id] += 1
    print('label distribution:\n', bin_sizes)

    # predict a single label
    print('single label accuracy: {:.2f}%'
          .format(100 * np.max(bin_sizes) / len(y)))

    # predict using delivery rate
    delivery_rate_predict = []
    for row in x:
        delivery_rate_predict.append(row[-6] / row[-5])
    delivery_rate_predict = discretize(delivery_rate_predict)

    accuracy = 100 * (delivery_rate_predict == y).sum() / len(y)
    print('delivery rate accuracy: {:.2f}%'.format(accuracy))

    return x, y


class Model:
    def __init__(self):
        # define model, loss function, and optimizer
        self.model = torch.nn.Sequential(
            torch.nn.Linear(DIM_IN, DIM_H1),
            torch.nn.ReLU(),
            torch.nn.Linear(DIM_H1, DIM_H2),
            torch.nn.ReLU(),
            torch.nn.Linear(DIM_H2, DIM_OUT),
        ).double().to(device=DEVICE)
        self.loss_fn = torch.nn.CrossEntropyLoss().to(device=DEVICE)
        self.optimizer = torch.optim.Adam(self.model.parameters(),
                                          lr=LEARNING_RATE,
                                          weight_decay=WEIGHT_DECAY)

    # perform one step of training (forward + backward + optimize)
    def train_step(self, input_data, output_data):
        x = torch.from_numpy(input_data).to(device=DEVICE)
        y = torch.from_numpy(output_data).to(device=DEVICE)

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
            x = torch.from_numpy(input_data).to(device=DEVICE)
            y = torch.from_numpy(output_data).to(device=DEVICE)

            y_scores = self.model(x)
            loss = self.loss_fn(y_scores, y)

            return loss.item()

    # compute accuracy of the classifier
    def compute_accuracy(self, input_data, output_data):
        correct = 0
        total = 0

        with torch.no_grad():
            x = torch.from_numpy(input_data).to(device=DEVICE)
            y = torch.from_numpy(output_data).to(device=DEVICE)

            y_scores = self.model(x)
            y_predicted = torch.max(y_scores, 1)[1].to(device=DEVICE)

            total += y.size(0)
            correct += (y_predicted == y).sum().item()

        return correct / total

    def save(self, model_path):
        torch.save(self.model.state_dict(), model_path)

    def load(self, model_path):
        self.model.load_state_dict(torch.load(model_path))
        self.model.eval()


def plot(losses, figure_path):
    fig, ax = plt.subplots()

    if 'training' in losses:
        ax.plot(losses['training'], 'g-', label='training')
    if 'validation' in losses:
        ax.plot(losses['validation'], 'r--', label='validation')

    ax.set_xlabel('Epoch')
    ax.set_ylabel('Loss')
    ax.grid()
    ax.legend()

    fig.savefig(figure_path, dpi=300, bbox_inches='tight', pad_inches=0.2)
    sys.stderr.write('Saved plot to {}\n'.format(figure_path))


def train(model, input_data, output_data):
    if TUNING:
        # permutate input and output data before splitting
        perm_indices = np.random.permutation(range(len(input_data)))
        input_data = input_data[perm_indices]
        output_data = output_data[perm_indices]

        # split training data into training/validation
        num_training = int(0.8 * len(input_data))
        train_input = input_data[:num_training]
        train_output = output_data[:num_training]
        validate_input = input_data[num_training:]
        validate_output = output_data[num_training:]
        print('training set size:', len(train_input))
        print('validation set size:', len(validate_input))

        validate_losses = []
    else:
        num_training = len(input_data)
        print('training set size:', num_training)

    train_losses = []
    # number of batches
    num_batches = int(np.ceil(num_training / BATCH_SIZE))

    # loop over the entire dataset multiple times
    for epoch_id in range(NUM_EPOCHS):
        # permutate data in each epoch
        perm_indices = np.random.permutation(range(num_training))

        running_loss = 0
        for batch_id in range(num_batches):
            start = batch_id * BATCH_SIZE
            end = min(start + BATCH_SIZE, num_training)
            batch_indices = perm_indices[start:end]

            # get a batch of input data
            batch_input = input_data[batch_indices]
            batch_output = output_data[batch_indices]

            running_loss += model.train_step(batch_input, batch_output)
        running_loss /= num_batches

        # print info
        if TUNING:
            train_loss = model.compute_loss(train_input, train_output)
            validate_loss = model.compute_loss(validate_input, validate_output)
            train_losses.append(train_loss)
            validate_losses.append(validate_loss)

            train_accuracy = 100 * model.compute_accuracy(
                    train_input, train_output)
            validate_accuracy = 100 * model.compute_accuracy(
                    validate_input, validate_output)

            print('epoch {:d}:\n'
                  '  training: loss {:.3f}, accuracy {:.2f}%\n'
                  '  validation loss {:.3f}, accuracy {:.2f}%'
                  .format(epoch_id + 1,
                          train_loss, train_accuracy,
                          validate_loss, validate_accuracy))
        else:
            train_losses.append(running_loss)
            print('epoch {:d}: training loss {:.3f}'
                  .format(epoch_id + 1, running_loss))

    # return losses for plotting
    losses = {}
    losses['training'] = train_losses
    if TUNING:
        losses['validation'] = validate_losses
    return losses


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('yaml_settings')
    parser.add_argument('--from', dest='time_start',
                        help='datetime in UTC conforming to RFC3339')
    parser.add_argument('--to', dest='time_end',
                        help='datetime in UTC conforming to RFC3339')
    parser.add_argument('--load', help='model to load from')
    parser.add_argument('--save', help='model to save to')
    parser.add_argument('--tune', action='store_true')
    parser.add_argument('--inference', action='store_true')
    parser.add_argument('--plot-loss', help='plot losses and save to a figure')
    parser.add_argument('--enable-gpu', action='store_true')
    args = parser.parse_args()

    if args.tune:
        global TUNING
        TUNING = True

    # set device to CPU or GPU
    if args.enable_gpu:
        if not torch.cuda.is_available():
            sys.exit('--enable-gpu but no CUDA is available')

        global DEVICE
        DEVICE = torch.device('cuda')
        torch.backends.cudnn.benchmark = True

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

    if args.inference:
        print('loss: {:.3f}, accuracy: {:.3f}'.format(
              model.compute_loss(input_data, output_data),
              model.compute_accuracy(input_data, output_data)))
    else:
        # train a neural network with data
        losses = train(model, input_data, output_data)

        if args.save:
            model.save(args.save)
            sys.stderr.write('Saved model to {}\n'.format(args.save))

        if args.plot_loss:
            plot(losses, args.plot_loss)


if __name__ == '__main__':
    main()
