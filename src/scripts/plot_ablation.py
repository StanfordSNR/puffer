#!/usr/bin/env python3

import sys
import argparse
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

from collect_data import collect_video_data, VIDEO_DURATION
import ttp

BIN_SIZE = 0.5
BIN_MAX = 20
TCP_INFO = ['delivery_rate', 'cwnd', 'in_flight', 'min_rtt', 'rtt']
TCP_SETTINGS = {'no cwnd in_flight': ['delivery_rate', 'min_rtt', 'rtt'],
                'no deliver_rate': ['cwnd', 'in_flight', 'min_rtt', 'rtt'],
                'no rtt': ['delivery_rate', 'cwnd', 'in_flight'],
                'origin': TCP_INFO,
                'no tcp_info': [],
               }
MODEL_PATH = '/home/ubuntu/models'
TCP_PATHS = {'no cwnd in_flight':\
                '/ablation/0101-0115-no-cwndinflight-bbr',
            'no deliver_rate':\
                '/ablation/0101-0115-no-delirate-bbr',
            'no rtt':\
                '/ablation/0101-0115-no-rtt-bbr',
            'origin':\
                '/ablation/0101-0115-past8-bbr',
            'no tcp_info':\
                '/ablation/0101-0115-no-tcp-info-bbr',
           }


def error(estimate, real):
    return (estimate - real) / real


def abs_error(estimate, real):
    dis_est = int((estimate + 0.5 * BIN_SIZE) / BIN_SIZE)
    dis_real = int((real + 0.5 * BIN_SIZE) / BIN_SIZE)
    return dis_est != dis_real


def ttp_discretized(trans_time):
    return int((trans_time + 0.5 * BIN_SIZE) / BIN_SIZE)

def ttp_map_dis_to_real(dis_time):
    if dis_time == 0:
        return BIN_SIZE * 0.25
    else:
        return dis_time * BIN_SIZE


def pred_error(dst, est_tput, verbose=False):
    assert(est_tput is not None)

    est_trans_time = dst['size'] / est_tput
    real_trans_time = dst['trans_time']

    dis_est = ttp_map_dis_to_real(ttp_discretized(est_trans_time))
    dis_real = ttp_map_dis_to_real(ttp_discretized(real_trans_time))

    if verbose:
        print(est_trans_time, ' ', real_trans_time)

    return abs(error(dis_est, dis_real))


def prepare_ttp_input(sess, ts, model):
    in_raw = ttp.prepare_input(sess, ts,
                               ttp.prepare_input_pre(sess, ts, model))

    assert(len(in_raw) == model.dim_in)
    input_data = model.normalize_input([in_raw], update_obs=False)

    return input_data


# the error using maximum likelihood estimation
def MLE_error(sess, ts, model):
    input_data = prepare_ttp_input(sess, ts, model)
    model.set_model_eval()
    pred = model.predict(input_data)
    return pred_error(sess[ts], sess[ts]['size'] / pred[0])


# the error using cross entropy loss
def CE_error(sess, ts, model):
    input_data = prepare_ttp_input(sess, ts, model)
    model.set_model_eval()
    scores = model.predict_distr(input_data).reshape(-1)
    dis_real = min(BIN_MAX, ttp_discretized(sess[ts]['trans_time']))
    return - np.log(scores[dis_real])


def calc_pred_error(d, models, error_func):
    midstream_err = {setting: [] for setting in TCP_SETTINGS}

    for session in d:
        for ts in d[session]:
            dst = d[session][ts]
            for setting in TCP_SETTINGS:
                midstream_err[setting].append( \
                    error_func(d[session], ts, models[setting]))

    return midstream_err


def plot_error_cdf(error_dict, time_start, time_end, xlabel):
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
    ax.set_xlabel(xlabel)
    ax.set_ylabel('CDF')

    figname = 'ablation.png'
    fig.savefig(figname, dpi=150, bbox_inches='tight')
    sys.stderr.write('Saved plot to {}\n'.format(figname))


def load_models(i):
    models = {}

    for setting in TCP_SETTINGS:
        model_path = MODEL_PATH + TCP_PATHS[setting] + '/py-{}.pt'.format(i)

        model = ttp.Model(past_chunks=8, tcp_info=TCP_SETTINGS[setting])
        model.load(model_path)
        sys.stderr.write('Loaded ttp model {} from {}\n' \
                         .format(setting, model_path))

        models[setting] = model

    return models


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('yaml_settings')
    parser.add_argument('--from', dest='time_start',
                        help='datetime in UTC conforming to RFC3339')
    parser.add_argument('--to', dest='time_end',
                        help='datetime in UTC conforming to RFC3339')
    parser.add_argument('--cc', help='filter input data by congestion control')
    parser.add_argument('--error-func', help='set different error function')
    args = parser.parse_args()

    video_data = collect_video_data(args.yaml_settings,
                                    args.time_start, args.time_end, args.cc)

    # for ablation study of tcp_info
    models = load_models(0)

    # choose error func
    error_func = None
    xlabel = None
    if args.error_func == 'MLE':
        error_func = MLE_error
        xlabel = 'MLE predict error'
    if args.error_func == 'CE':
        error_func = CE_error
        xlabel = 'CE predict error'

    midstream_err = calc_pred_error(video_data, models, error_func)

    #plot CDF graph of mistream prediction errors
    plot_error_cdf(midstream_err, args.time_start, args.time_end, xlabel)

if __name__ == '__main__':
    main()
