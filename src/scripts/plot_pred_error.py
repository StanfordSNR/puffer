#!/usr/bin/env python3

import sys
import yaml
import argparse
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

from helpers import (
    connect_to_influxdb, connect_to_postgres, query_measurement,
    retrieve_expt_config)
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


def load_models(ttp_model_path):
    ttp_model = ttp.Model()
    ttp_model.load(ttp_model_path)
    sys.stderr.write('Loaded ttp model from {}\n'.format(ttp_model_path))

    return {'ttp': ttp_model}


def filter_video_data_by_cc(d, yaml_settings, required_cc):
    # cache of Postgres data: experiment 'id' -> json 'data' of the experiment
    expt_id_cache = {}

    # create a client connected to Postgres
    postgres_client = connect_to_postgres(yaml_settings)
    postgres_cursor = postgres_client.cursor()

    sessions_to_remove = []
    for session in d:
        expt_id = int(session[-1])
        expt_config = retrieve_expt_config(expt_id, expt_id_cache,
                                           postgres_cursor)
        cc = expt_config['cc']
        if cc != required_cc:
            sessions_to_remove.append(session)

    for session in sessions_to_remove:
        del d[session]

    postgres_cursor.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('yaml_settings')
    parser.add_argument('--from', dest='time_start',
                        help='datetime in UTC conforming to RFC3339')
    parser.add_argument('--to', dest='time_end',
                        help='datetime in UTC conforming to RFC3339')
    parser.add_argument('--cc', help='filter input data by congestion control')
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

    # modify video_data in place by deleting sessions not with args.cc
    filter_video_data_by_cc(video_data, yaml_settings, args.cc)

    # load models
    models = load_models(args.ttp_model)

    # calculate prediction error
    midstream_err = calc_pred_error(video_data, models)

    # plot CDF graph of mistream prediction errors
    plot_error_cdf(midstream_err, args.time_start, args.time_end)


if __name__ == '__main__':
    main()
