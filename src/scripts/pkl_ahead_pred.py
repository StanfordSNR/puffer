#!/usr/bin/env python3

import os
import sys
import yaml
import pickle
import json
import argparse
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

from helpers import (
    connect_to_influxdb, connect_to_postgres, query_measurement,
    retrieve_expt_config, filter_video_data_by_cc, get_abr_cc)
from collect_data import video_data_by_session, VIDEO_DURATION
import ttp

BIN_SIZE = 0.5
BIN_MAX = 20

MODEL_PATH = '/home/ubuntu/models'
TTP_PATH= '/ablation/0101-0115-past8-bbr'


def absolute_error(estimate, real):
    return abs(ttp_discretized(estimate) - ttp_discretized(real))


def zero_one_error(estimate, real):
    return int((estimate - real) > 0.25)


def inter_error(estimate, real):
    return int(ttp_discretized(estimate) != ttp_discretized(real))


def cut_error(estimate, real):
    return max(abs(estimate - real) - 0.25, 0)


def ttp_discretized(trans_time):
    return int((trans_time + 0.5 * BIN_SIZE) / BIN_SIZE)


def ttp_map_dis_to_real(dis_time):
    if dis_time == 0:
        return BIN_SIZE * 0.25
    else:
        return dis_time * BIN_SIZE


def pred_error(real, est, err_func):
    if err_func == 'zero_one':
        return zero_one_error(est, real)
    elif err_func == 'inter':
        return inter_error(est, real)
    elif err_func == 'dis':
        return absolute_error(est, real)


def prepare_ttp_input(sess, ts, model, curr_size=None):
    in_raw = ttp.prepare_input(sess, ts,
        ttp.prepare_input_pre(sess, ts, model) , curr_size)

    assert(len(in_raw) == model.dim_in)
    input_data = model.normalize_input([in_raw], update_obs=False)

    return input_data


def ttp_pred(sess, ts, model, curr_size=None):
    input_data = prepare_ttp_input(sess, ts, model, curr_size)
    model.set_model_eval()
    return model.predict(input_data)


# HM prediction
def harmonic_mean(sess, ts):
    past_tputs = []

    for i in range(1, 6):
        prev_ts = ts - i * VIDEO_DURATION
        if prev_ts not in sess:
            return None

        prev_tput = sess[prev_ts]['size'] / sess[prev_ts]['trans_time']
        past_tputs.append(prev_tput)  # byte/second

        hm_tput = len(past_tputs) / np.sum(1 / np.array(past_tputs))

    return min(10, sess[ts]['size'] / hm_tput)

def last(sess, ts):
    prev_ts = ts - VIDEO_DURATION
    last_tput = sess[prev_ts]['size'] / sess[prev_ts]['trans_time']
    return min(10, sess[ts]['size'] / last_tput)


def pred_single_from_distr(distr, err_func):
    if err_func == 'zero_one' or err_func == 'inter':
        pred = np.argmax(distr)
        return ttp_map_dis_to_real(pred)
    if err_func == 'dis':
        tmp = 0
        pred = 0
        for p in distr:
            if tmp + p > 0.5:
                return ttp_map_dis_to_real(pred)
            pred += 1
            tmp += p


def collect_pred_error(d, models, args, expt_id_cache):
    err_func = args.err_func

    settings = ['HM', 'first', 'all', 'last']

    err = {setting: {i:[] for i in range(5)} for setting in settings}

    tot = len(d)
    cnt = 0
    print_every = 1000

    for session in d:
        expt_id = session[-1]

        if not args.emu:
            expt_config = expt_id_cache[int(expt_id)]
            abr_cc = get_abr_cc(expt_config)
        else:
            abr_cc = tuple(expt_id.split('+'))

        cnt += 1

        if cnt % print_every == 0:
            x = cnt / tot * 100
            sys.stderr.write('Finish {0:.2f} % ({1}/{2})\n' \
                .format(float(x), str(cnt), str(tot)))

        if abr_cc[1] == 'cubic':
            continue

        for ts in d[session]:
            dst = d[session][ts]

            if args.cut_small and dst['trans_time'] < 0.5:
                continue

            if harmonic_mean(d[session], ts) == None:
                continue

            if ts + 4 * VIDEO_DURATION not in d[session]:
                continue

            hm_est = harmonic_mean(d[session], ts)
            first_est = ttp_pred(d[session], ts, models[0])
            last_est = last(d[session], ts)

            for i in range(5):
                est = ttp_pred(d[session], ts, models[i])

                nts = ts + i * VIDEO_DURATION
                real = min(10, d[session][nts]['trans_time'])

                err['HM'][i].append(pred_error(real, hm_est, err_func))
                err['first'][i].append(pred_error(real, first_est, err_func))
                err['all'][i].append(pred_error(real, est, err_func))
                err['last'][i].append(pred_error(real, last_est, err_func))

    #            print(setting, ':', real, ', ', est)

    return err


#def calc_pred(d, models):
#    pred_data = []
#
#    for session in d:
#        for ts in d[session]:
#            dst = d[session][ts]
#
#            est = harmonic_mean(d[session], ts)
#            if est == None:
#                continue
#
#            pred_data_ts = {'size': dst['size'],
#                             'trans_time': dst['trans_time'],
#                             'HM': est,
#                            }
#
#            for setting in models:
#                pred_data_ts[setting] = list(ttp_pred_distr(d[session], ts,
#                                                        models[setting]))
#
#            pred_data.append(pred_data_ts)
#    return pred_data
#

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
    fig.savefig(figname)
    sys.stderr.write('Saved plot to {}\n'.format(figname))


def get_figure(d):
    x = {}
    for abr in d:
        x[abr] = {'num': [len(d[abr][i]) for i in range(5)],
                  'mean': [np.mean(d[abr][i]) for i in range(5)],
                  'std': [np.std(d[abr][i]) for i in range(5)],
                 }

    return x


def print_d(d, fp):
    for abr in d:
        fp.write(abr + ':')
        ds = d[abr]
        for k in ds:
            fp.write(' ' + str(ds[k]) + ',')
        fp.write('\n')


def load_models():
    models = []

    for i in range(5):
        model_path = MODEL_PATH + TTP_PATH + '/py-{}.pt'.format(i)
        model = ttp.Model()
        model.load(model_path)
        sys.stderr.write('Load ttp model {} from {}\n' \
                         .format(str(i), model_path))

        models.append(model)

    return models


def plot(args, expt_id_cache):

    if args.cut_small:
        cut_small = 'cs'
    else:
        cut_small = ''

    pre_dp = '{}_{}_{}.pickle'.format(args.pre_dp, args.err_func, cut_small)
    output = '{}_{}_{}.txt'.format(args.pre_dp, args.err_func, cut_small)

    if os.path.isfile(pre_dp):
        with open(pre_dp, 'rb') as fp:
            d = pickle.load(fp)
    else:
        # for ablation study of tcp_info
        models = load_models()
        print('Finish loading models!')

        with open(args.video_data_pickle, 'rb') as fh:
            video_data = pickle.load(fh)
            print('Finish loading video data!')

        d = collect_pred_error(video_data, models, args, expt_id_cache)

        with open(pre_dp, 'wb') as fp:
            pickle.dump(d, fp)

    f = get_figure(d)

    with open(output, 'w') as fp:
        print_d(f, fp)
        sys.stderr.write('Print result to {}\n'.format(output))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('-v', '--video-data-pickle', required=True)
    parser.add_argument('--emu', action='store_true')
    parser.add_argument('-e', '--expt-id-cache',
                        default='expt_id_cache.pickle')
    parser.add_argument('--pre-dp', default='ahead_pred_train')
    parser.add_argument('--err-func', default='inter')
    parser.add_argument('--cut-small', action='store_true')

    args = parser.parse_args()

    with open(args.expt_id_cache, 'rb') as fh:
        expt_id_cache = pickle.load(fh)

    plot(args, expt_id_cache)

if __name__ == '__main__':
    main()
