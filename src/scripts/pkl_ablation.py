#!/usr/bin/env python3

import os
import sys
import yaml
import pickle
import json
import argparse
import numpy as np
import matplotlib
#matplotlib.use('Agg')
import matplotlib.pyplot as plt

from helpers import (
    connect_to_influxdb, connect_to_postgres, query_measurement,
    retrieve_expt_config, filter_video_data_by_cc, get_abr_cc)
from collect_data import video_data_by_session, VIDEO_DURATION
import ttp

BIN_SIZE = 0.5
BIN_MAX = 20


TCP_INFO = ['delivery_rate', 'cwnd', 'in_flight', 'min_rtt', 'rtt']
NO_CWND = ['delivery_rate', 'min_rtt', 'rtt']
NO_DELI = ['cwnd', 'in_flight', 'min_rtt', 'rtt']
NO_RTT = ['delivery_rate', 'cwnd', 'in_flight']
TTP_SETTINGS = {'no cwnd in_flight': [8, NO_CWND],
                'no delivery_rate': [8, NO_DELI],
                'no rtt': [8, NO_RTT],
                'no tcp_info': [8, []],
                'origin': [8, TCP_INFO],
                'past 0': [0, TCP_INFO],
                'past 1': [1, TCP_INFO],
                'past 2': [2, TCP_INFO],
                'past 4': [4, TCP_INFO],
                'past 8': [8, TCP_INFO],
                'past 16': [16, TCP_INFO],
                'continue': [8, TCP_INFO],
                'no past no cwnd inflight': [0, NO_CWND],
                'no past no delivery rate': [0, NO_DELI],
                'no past no rtt': [0, NO_RTT],
                'past 1 no tcp': [1, []],
                'past 2 no tcp': [2, []],
                'past 4 no tcp': [4, []],
                'past 16 no tcp': [16, []],
               }

MODEL_PATH = '/home/ubuntu/models'
TTP_PATHS = {'no cwnd in_flight': \
                '/ablation/0101-0115-no-cwmdinflight-bbr',
             'no delivery_rate': \
                '/ablation/0101-0115-no-delirate-bbr',
             'no rtt': \
                '/ablation/0101-0115-no-rtt-bbr',
             'no tcp_info': \
                '/ablation/0101-0115-no-tcp-info-bbr',
             'origin': \
                '/ablation/0101-0115-past8-bbr',
             'past 0': \
                '/ablation/0101-0115-past0-bbr',
             'past 1': \
                '/ablation/0101-0115-past1-bbr',
             'past 2': \
                '/ablation/0101-0115-past2-bbr',
             'past 4': \
                '/ablation/0101-0115-past4-bbr',
             'past 8': \
                '/ablation/0101-0115-past8-bbr',
             'past 16': \
                '/ablation/0101-0115-past16-bbr',
             'continue': \
                '/puffer-models-0122/bbr-20190122-1',
             'no past no cwnd inflight': \
                '/ablation/0101-0115-past0-no-cwnd-inflight-bbr',
             'no past no delivery rate': \
                '/ablation/0101-0115-past0-no-delirate-bbr',
             'no past no rtt': \
                '/ablation/0101-0115-past0-no-rtt-bbr',
             'past 1 no tcp': '/ablation/0101-0115-past1-no-tcp-bbr',
             'past 2 no tcp': '/ablation/0101-0115-past2-no-tcp-bbr',
             'past 4 no tcp': '/ablation/0101-0115-past4-no-tcpinfo-bbr',
             'past 16 no tcp': '/ablation/0101-0115-past16-no-tcp-bbr',
            }

ABR_SETTINGS = {'no_past': ['no past no cwnd inflight',
                            'no past no delivery rate',
                            'no past no rtt',
                            'past 0',
                            'past 8',
                            'no tcp_info',
                            'HM'],
                'no_tcp': ['past 1 no tcp',
                           'past 2 no tcp',
                           'past 4 no tcp',
                           'no tcp_info',
                           'past 16 no tcp',
                           'past 8',
                           'HM'],
                'tcp': ['no cwnd in_flight',
                        'no delivery_rate',
                        'no rtt',
                        'no tcp_info',
                        'past 8',
                        'HM']
               }

def absolute_error(estimate, real):
    return abs(estimate - real)


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


def ttp_pred_distr(sess, ts, model, curr_size=None):
    input_data = prepare_ttp_input(sess, ts, model, curr_size)
    model.set_model_eval()
    scores = np.array(model.predict_distr(input_data)).reshape(-1)
    return scores


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

    err = {setting: [] for setting in models}

    tot = len(d)
    cnt = 0
    print_every = 100

    for session in d:
        expt_id = session[-1]

        expt_config = expt_id_cache[int(expt_id)]
        abr_cc = get_abr_cc(expt_config)

        cnt += 1

        if cnt % print_every == 0:
            x = cnt / tot * 100
            sys.stderr.write('Finish {0:.2f} % ({1}/{2})\n' \
                    .format(float(x), str(cnt), str(tot)))

        if abr_cc[1] == 'cubic':
            continue

        for ts in d[session]:
            dst = d[session][ts]

            if args.filt_fast and \
               dst['size'] / dst['trans_time'] / 125000 > 6:
                continue

            if harmonic_mean(d[session], ts) == None:
                continue

            for setting in models:
                if setting == 'HM':
                    est = harmonic_mean(d[session], ts)
                else:
                    distr = ttp_pred_distr(d[session], ts, models[setting][0])
                    est = pred_single_from_distr(distr, err_func)

                real = min(10, dst['trans_time'])

        #        print(setting, real, est)
                err[setting].append(pred_error(real, est, err_func))
    #            print(setting, ':', real, ', ', est)

    return err


def filt_fast(video_data, min_tput):
    Mbps_unit = 125000

    del_video_sess = []

    for sess in video_data:
        avg_tput = np.mean([t['size'] / t['trans_time'] / Mbps_unit \
                            for t in video_data[sess].values()])

        if avg_tput < min_tput:
            continue

        del_video_sess.append(sess)

    del_buffer = 0

    for sess in del_video_sess:
        del video_data[sess]

    print('Number of deleted video_data sessions:', len(del_video_sess))
    print('Number of left video_data sessions:', len(video_data))

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
        x[abr] = {'num': len(d[abr]),
                  'mean': np.mean(d[abr]),
                  'std': np.std(d[abr]),
                 }

    return x


def print_d(d, fp):
    for abr in d:
        fp.write(abr + ':')
        ds = d[abr]
        for k in ds:
            fp.write(' ' + str(ds[k]) + ',')
        fp.write('\n')


def load_models(terms):
    models = {}

    for term in terms:
        if term == 'HM':
            models['HM'] = None
            continue

        models[term] = []
        for i in range(5):
            model_path = MODEL_PATH + TTP_PATHS[term] + '/py-{}.pt'.format(i)

            if not os.path.isfile(model_path):
                model_path = MODEL_PATH + TTP_PATHS[term] \
                             + '/py-{}-checkpoint-250.pt'.format(i)

            model = ttp.Model(past_chunks=TTP_SETTINGS[term][0],
                              tcp_info=TTP_SETTINGS[term][1])
            model.load(model_path)
            sys.stderr.write('Load ttp model {} from {}\n' \
                         .format(term, model_path))

            models[term].append(model)

    return models


def plot(expt_id_cache, args):
    if args.filt_fast:
        filt = '_slow'
    else:
        filt = ''

    pre_dp = '{}_{}_{}{}.pickle'.format(args.pre_dp, args.task, args.err_func,
                                         filt)
    output = '{}_{}_{}{}.txt'.format(args.pre_dp, args.task, args.err_func,
                                      filt)

    if os.path.isfile(pre_dp):
        with open(pre_dp, 'rb') as fp:
            d = pickle.load(fp)
    else:
        # for ablation study of tcp_info
        models = {}
        models = load_models(ABR_SETTINGS[args.task])
        print('Finish loading models!')

        with open(args.video_data_pickle, 'rb') as fh:
            video_data = pickle.load(fh)
            print('Finish loading video data!')

  #      if args.filt_fast:
   #         filt_fast(video_data, 6)

        d = collect_pred_error(video_data, models, args, expt_id_cache)

        with open(pre_dp, 'wb') as fp:
            pickle.dump(d, fp)

    f = get_figure(d)

    with open(output, 'w') as fp:
        print_d(f, fp)
        sys.stderr.write('Print result to {}\n'.format(output))

#    xlabel = None
 #   if args.error_func == 'absolute':
  #      xlabel = 'Absolute Predict Error'
   # if args.error_func == 'ave_dis':
    #    xlabel = 'Average Distance Predict Error'

    #pred_data = calc_pred(video_data, models)

    #with open('video-data-from-' + args.time_start + '-to-' \
     #       + args.time_end + '.txt', 'w') as fp:
      #  for term in pred_data:
       #     fp.write(str(term))
        #    fp.write('\n')

    #print("Write finished!")

    #return

    # plot CDF graph of mistream prediction errors
    #plot_error_cdf(error, args.time_start, args.time_end, xlabel)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('-v', '--video-data-pickle', required=True)
    parser.add_argument('--emu', action='store_true')
    parser.add_argument('-e', '--expt-id-cache',
                        default='expt_id_cache.pickle')
    parser.add_argument('--pre-dp', default='pred_error_train')
    parser.add_argument('--err-func', default='zero_one')
    parser.add_argument('--task', default='no_past')
    parser.add_argument('--filt-fast', action='store_true')

    args = parser.parse_args()

    with open(args.expt_id_cache, 'rb') as fh:
        expt_id_cache = pickle.load(fh)

    plot(expt_id_cache, args)

if __name__ == '__main__':
    main()
