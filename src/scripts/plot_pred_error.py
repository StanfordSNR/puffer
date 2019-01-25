#!/usr/bin/env python3

import sys
import yaml
import argparse
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

from helpers import connect_to_influxdb, query_measurement
from collect_data import video_data_by_session, VIDEO_DURATION
import ttp


BIN_SIZE = 0.5


def error(estimate, real):
    return (estimate - real) / real


def abs_error(estimate, real):
    dis_est = int((estimate + 0.5 * BIN_SIZE) / BIN_SIZE)
    dis_real = int((real + 0.5 * BIN_SIZE) / BIN_SIZE)
    return dis_est != dis_real


def discretized(trans_time):
    dis_time = int((trans_time + 0.5 * BIN_SIZE) / BIN_SIZE)
    if dis_time == 0:
        return BIN_SIZE * 0.25
    else:
        return dis_time * BIN_SIZE


def pred_error(dst, est_tput, verbose=False):
    assert(est_tput is not None)

    est_trans_time = dst['size'] / est_tput
    real_trans_time = dst['trans_time']

    dis_est = discretized(est_trans_time)
    dis_real = discretized(real_trans_time)

    if verbose:
        print(est_trans_time, ' ', real_trans_time)

    return abs(error(dis_est, dis_real))


def last_sample(sess, ts):
    last_ts = ts - VIDEO_DURATION
    if last_ts not in sess:
        return None

    return sess[last_ts]['size'] / sess[last_ts]['trans_time']  # byte/second


def harmonic_mean(sess, ts):
    past_tputs = []

    for i in range(1, 6):
        prev_ts = ts - i * VIDEO_DURATION
        if prev_ts not in sess:
            return None

        prev_tput = sess[prev_ts]['size'] / sess[prev_ts]['trans_time']
        past_tputs.append(prev_tput)  # byte/second

    hm_tput = len(past_tputs) / np.sum(1 / np.array(past_tputs))
    return hm_tput


def hidden_markov(sess, ts):
    # TODO
    pass


def puffer_ttp(sess, ts, model):
    in_raw = ttp.prepare_input(sess, ts,
                               ttp.prepare_input_pre(sess, ts, model))

    assert(len(in_raw) == model.dim_in)
    input_data = model.normalize_input([in_raw], update_obs=False)
    model.set_model_eval()

    pred = model.predict(input_data)
    return sess[ts]['size'] / pred[0]


def calc_pred_error(d, models):
    midstream_err = {'TCP': [], 'LS': [], 'HM': [], 'HMM': [], 'TTP': []}

    for session in d:
        for ts in d[session]:
            dst = d[session][ts]

            # TCP info
            est_tput = dst['delivery_rate']  # byte/second
            if est_tput is not None:
                midstream_err['TCP'].append(pred_error(dst, est_tput))

            # Last Sample
            est_tput = last_sample(d[session], ts)  # byte/second
            if est_tput is not None:
                midstream_err['LS'].append(pred_error(dst, est_tput))

            # Harmonic Mean (MPC)
            est_tput = harmonic_mean(d[session], ts)  # byte/second
            if est_tput is not None:
                midstream_err['HM'].append(pred_error(dst, est_tput))

            # Hidden Markov Model (CS2P)
            est_tput = hidden_markov(d[session], ts)  # byte/second
            if est_tput is not None:
                midstream_err['HMM'].append(pred_error(dst, est_tput))

            # Puffer TTP
            est_tput = puffer_ttp(d[session], ts, models['ttp'])
            if est_tput is not None:
                midstream_err['TTP'].append(pred_error(dst, est_tput))

    # TODO: calculate errors for initial chunks and lookahead horizon

    return midstream_err


def plot_error_cdf(error_dict, time_start, time_end):
    fig, ax = plt.subplots()

    x_min = 0
    x_max = 1
    num_bins = 100
    for estimator, error_list in error_dict.items():
        if not error_list:
            continue

        counts, bin_edges = np.histogram(error_list, bins=num_bins,
                                         range=(x_min, x_max))
        x = bin_edges
        y = np.cumsum(counts) / len(error_list)
        y = np.insert(y, 0, 0)  # prepend 0

        ax.plot(x, y, label=estimator)

    ax.set_xlim(x_min, x_max)
    ax.set_ylim(0, 1)
    ax.legend()
    ax.grid()

    title = ('Transmission Time Prediction Accuracy\n[{}, {}] (UTC)'
             .format(time_start, time_end))
    ax.set_title(title)
    ax.set_xlabel('Absolute Prediction Error')
    ax.set_ylabel('CDF')

    figname = 'trans_time_error.png'
    fig.savefig(figname, dpi=150, bbox_inches='tight')
    sys.stderr.write('Saved plot to {}\n'.format(figname))


def plot_session_duration_and_throughput(d, time_start, time_end):
    session_durations = []
    tputs = []
    total_rebuffer = []
    rebuffer_percent = []
    for session in d:
        ds = d[session]
        if len(ds) < 5:
            # Dont count sessions that dont deliver 10 seconds of video
            continue
        session_durations.append(len(ds) * 2.002 / 60)  # minutes
        tput_sum = 0
        tput_count = 0
        first_ts = True
        second_ts = True
        for next_ts in sorted(ds.keys()):  # Need to iterate in order!
            if 'trans_time' not in ds[next_ts]:
                continue
            if first_ts:
                first_ts = False
            elif second_ts:  # TODO: Better method for detecting startup delay
                startup_delay = ds[next_ts]['cum_rebuffer']
                second_ts = False
            tput_sum += (ds[next_ts]['size'] * 8 / ds[next_ts]['trans_time'] /
                         1000000)  # Mbps
            tput_count += 1
        if tput_count == 0:
            continue
        if not first_ts:
            # Rebuffer time = cum rebuffer of last chunk - startup delay
            total_rebuffer.append(ds[sorted(ds.keys())[-1]]['cum_rebuffer'] -
                                  startup_delay)
            rebuffer_percent.append(total_rebuffer[-1] / (session_durations[-1] + total_rebuffer[-1]) * 100)
        tputs.append(tput_sum/tput_count)  # Average tput for session

    fig, ax = plt.subplots()
    ax.hist(session_durations, bins=100000, normed=1, cumulative=True,
            histtype='step')
    ax.grid(True)
    ax.set_xlabel('Session duration (minutes)')
    ax.set_ylabel('CDF')
    title = ('Session Duration from [{}, {}] (UTC)'
             .format(time_start, time_end))
    xmin, xmax = ax.get_xlim()
    xmax = 50
    ax.set_xlim(0, xmax)
    ax.set_title(title)
    fig.savefig('session_duration.png', dpi=150, bbox_inches='tight',
                pad_inches=0.2)
    sys.stderr.write('Saved session duration plot to {}\n'
                     .format('session_duration.png'))

    fig, ax = plt.subplots()
    ax.hist(tputs, bins=100000, normed=1, cumulative=True,
            histtype='step')
    ax.grid(True)
    ax.set_xlabel('Session throughput (Mbps)')
    ax.set_ylabel('CDF')
    title = ('Session Throughputs from [{}, {}] (UTC)'
             .format(time_start, time_end))
    xmin, xmax = ax.get_xlim()
    xmax = 100
    ax.set_xlim(0, xmax)
    ax.set_title(title)
    fig.savefig('session_throughputs.png', dpi=150, bbox_inches='tight',
                pad_inches=0.2)
    sys.stderr.write('Saved session throughputs plot to {}\n'
                     .format('session_throughputs.png'))

    fig, ax = plt.subplots()
    ax.hist(rebuffer_percent, bins=100000, normed=1, cumulative=True,
            histtype='step')
    ax.grid(True)
    ax.set_xlabel('% Rebuffer (excluding startup) - all ABR')
    ax.set_ylabel('CDF')
    title = ('Rebuffer % (all ABR) from [{}, {}) \
             (UTC)'.format(time_start, time_end))
    xmin, xmax = ax.get_xlim()
    # xmax = 100
    ax.set_xlim(0, xmax)
    ax.set_title(title)
    fig.savefig('rebuffer_percent.png', dpi=150, bbox_inches='tight',
                pad_inches=0.2)
    sys.stderr.write('Saved rebuffer percent plot to {}\n'
                     .format('rebuffer_percent.png'))

    print('Percentage of all sessions with a rebuffer: ' +
          str(np.count_nonzero(total_rebuffer) / len(session_durations)))


def load_models(ttp_model_path):
    ttp_model = ttp.Model()
    ttp_model.load(ttp_model_path)
    sys.stderr.write('Loaded ttp model from {}\n'.format(ttp_model_path))

    return {'ttp': ttp_model}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('yaml_settings')
    parser.add_argument('--from', dest='time_start',
                        help='datetime in UTC conforming to RFC3339')
    parser.add_argument('--to', dest='time_end',
                        help='datetime in UTC conforming to RFC3339')
    parser.add_argument('--ttp-model', help='path to a "py-0.pt"')
    args = parser.parse_args()

    yaml_settings_path = args.yaml_settings
    with open(yaml_settings_path, 'r') as fh:
        yaml_settings = yaml.safe_load(fh)

    # create an InfluxDB client and perform queries
    influx_client = connect_to_influxdb(yaml_settings)

    # query data from video_sent and video_acked
    video_sent_results = query_measurement(influx_client, 'video_sent',
                                           args.time_start, args.time_end)
    video_acked_results = query_measurement(influx_client, 'video_acked',
                                            args.time_start, args.time_end)
    video_data = video_data_by_session(video_sent_results, video_acked_results)

    # load models
    models = load_models(args.ttp_model)

    # calculate prediction error
    midstream_err = calc_pred_error(video_data, models)

    # plot CDF graph of mistream prediction errors
    plot_error_cdf(midstream_err, args.time_start, args.time_end)


if __name__ == '__main__':
    main()
