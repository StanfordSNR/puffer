#!/usr/bin/env python3

import os
import sys
import argparse
import yaml
import copy
import json
import numpy as np

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

from multiprocessing import Pool

from helpers import connect_to_influxdb, time_pair, datetime_iter_list
from stream_processor import VideoStream
from ttp import Model, PKT_BYTES
from ttp2 import Model as Linear_Model
from ttp_ab import Model as Abl_Model
from plot_helpers import *


VIDEO_DURATION = 180180
MID_SIZE = 766929
PAST_CHUNKS = 8
TCP_INFO = ['delivery_rate', 'cwnd', 'in_flight', 'min_rtt', 'rtt']
ABL_COMMON_PATH = '/home/ubuntu/models/puffer_abl/'
PRINT_EVERY = 10000

args = None

abl_names = ['ttp_nh', 'ttp_nhd', 'ttp_nhci', 'ttp_nhmr']
abl_settings = {'ttp_nh': (0, TCP_INFO),
                'ttp_nhd': (0, ['cwnd', 'in_flight', 'min_rtt', 'rtt']),
                'ttp_nhci': (0, ['delivery_rate', 'min_rtt', 'rtt']),
                'ttp_nhmr': (0, ['delivery_rate', 'cwnd', 'in_flight'])}
abl_paths = {'ttp_nh': ABL_COMMON_PATH + 'abl_dcimr_0/py-0.pt',
             'ttp_nhd': ABL_COMMON_PATH + 'abl_cimr_0/py-0.pt',
             'ttp_nhci': ABL_COMMON_PATH + 'abl_dmr_0/py-0.pt',
             'ttp_nhmr': ABL_COMMON_PATH + 'abl_dci_0/py-0.pt'}

terms = ['bin', 'l1', 'l2']
predictors = ['ttp', 'ttp_mle', 'ttp_tp', 'linear', 'tcp_info', 'harmonic']\
             + abl_names


def process_session(session, s, tot, result, ttp_model, linear_model,
                    abl_models, expt):
    for curr_ts in s:
        chunks = []

        for i in reversed(range(PAST_CHUNKS + 1)):
            ts = curr_ts - i * VIDEO_DURATION
            if ts not in s:
                break
            chunks.append(s[ts])

        if len(chunks) != PAST_CHUNKS + 1:
            continue

        curr_chunk_size = chunks[0]['size']

        tot[0] += 1
        (raw_in, raw_out) = prepare_intput_output(chunks)
        unit_raw_in = copy.deepcopy(raw_in)
        unit_raw_in[0][-1] = MID_SIZE / PKT_BYTES

        ttp_distr = model_pred(ttp_model, raw_in)

        linear_distr = model_pred(linear_model, raw_in)

        ttp_tp_distr = model_pred(ttp_model, unit_raw_in)
        bin_ttp_tp_out = distr_bin_pred(ttp_tp_distr)
        bin_ttp_tp_out[0] *= curr_chunk_size / MID_SIZE

        if chunks[0]['delivery_rate'] < 1e-5:
            tcp_info_out = 60
        else:
            tcp_info_out = min(60,
                curr_chunk_size / chunks[0]['delivery_rate'])

        # ttp
        bin_ttp_out = distr_bin_pred(ttp_distr)
        l1_ttp_out = distr_l1_pred(ttp_distr)
        l2_ttp_out = distr_l2_pred(ttp_distr)

        result['ttp']['bin'] += bin_acc(bin_ttp_out[0], raw_out[0])
        result['ttp']['l1'] += l1_loss(l1_ttp_out[0], raw_out[0])
        result['ttp']['l2'] += l2_loss(l2_ttp_out[0], raw_out[0])

        # ttp_mle
        result['ttp_mle']['bin'] += bin_acc(bin_ttp_out[0], raw_out[0])
        result['ttp_mle']['l1'] += l1_loss(bin_ttp_out[0], raw_out[0])
        result['ttp_mle']['l2'] += l2_loss(bin_ttp_out[0], raw_out[0])

        # ttp_tp
        result['ttp_tp']['bin'] += bin_acc(bin_ttp_tp_out[0], raw_out[0])
        result['ttp_tp']['l1'] += l1_loss(bin_ttp_tp_out[0], raw_out[0])
        result['ttp_tp']['l2'] += l2_loss(bin_ttp_tp_out[0], raw_out[0])

        # tcp_info
        result['tcp_info']['bin'] += bin_acc(tcp_info_out, raw_out[0])
        result['tcp_info']['l1'] += l1_loss(tcp_info_out, raw_out[0])
        result['tcp_info']['l2'] += l2_loss(tcp_info_out, raw_out[0])

        # linear
        bin_linear_out = distr_bin_pred(linear_distr)
        l1_linear_out = distr_l1_pred(linear_distr)
        l2_linear_out = distr_l2_pred(linear_distr)

        result['linear']['bin'] += bin_acc(bin_linear_out[0], raw_out[0])
        result['linear']['l1'] += l1_loss(l1_linear_out[0], raw_out[0])
        result['linear']['l2'] += l2_loss(l2_linear_out[0], raw_out[0])

        # abl_models
        for name, abl_model in abl_models.items():
            past_chunks = abl_settings[name][0]
            tcp_info = abl_settings[name][1]
            abl_raw_in, _ = prepare_intput_output(chunks[:past_chunks+1],
                                                  tcp_info=tcp_info)
            abl_distr = model_pred(abl_model, abl_raw_in)

            bin_abl_out = distr_bin_pred(abl_distr)
            l1_abl_out = distr_l1_pred(abl_distr)
            l2_abl_out = distr_l2_pred(abl_distr)

            result[name]['bin'] += bin_acc(bin_abl_out[0], raw_out[0])
            result[name]['l1'] += l1_loss(l1_abl_out[0], raw_out[0])
            result[name]['l2'] += l2_loss(l2_abl_out[0], raw_out[0])


        # harmonic
        harm_out = harmonic_pred(chunks[:6])

        result['harmonic']['bin'] += bin_acc(harm_out[0], raw_out[0])
        result['harmonic']['l1'] += l1_loss(harm_out[0], raw_out[0])
        result['harmonic']['l2'] += l2_loss(harm_out[0], raw_out[0])


def worker(start_time, end_time):
    tot = [0]
    result = {predictor: {term: 0 for term in terms} for predictor in predictors}
    abl_models = {name: Abl_Model(past_chunks=abl_settings[name][0],
                    tcp_info=abl_settings[name][1])\
                    for name in abl_names}
    expt = None

    ttp_model = Model()
    linear_model = Linear_Model()


    with open(args.yaml_settings, 'r') as fh:
        yaml_settings = yaml.safe_load(fh)

    with open(args.expt, 'r') as fh:
        expt = json.load(fh)

    # create an InfluxDB client and perform queries
    influx_client = connect_to_influxdb(yaml_settings)

    ttp_model.load(args.ttp_path)

    linear_model.load("/home/ubuntu/models/puffer_ttp/linear/py-0.pt")

    for name, model in abl_models.items():
        model.load(abl_paths[name])

    video_stream = VideoStream(
            lambda session, s, tot=tot, result=result, ttp_model=ttp_model,
                    linear_model=linear_model, abl_models=abl_models,
                    expt=expt: process_session(session, s, tot, result,
                         ttp_model, linear_model, abl_models, expt))
    video_stream.process(influx_client, start_time, end_time)

    return (tot[0], result)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('yaml_settings')
    parser.add_argument('-t', action='append', type=time_pair, required=True)
    parser.add_argument('--expt', help='e.g., expt_cache.json')
    parser.add_argument('-o', '--output', required=True)
    parser.add_argument('--ttp-path', dest='ttp_path', required=True)
    global args
    args = parser.parse_args()


    pool = Pool(processes=12)
    procs = []

    for s_str, e_str in datetime_iter_list(args.t, 4):
        procs.append((pool.apply_async(worker, (s_str, e_str,)),
                      s_str, e_str))

    tot = 0
    result = {predictor: {term: 0 for term in terms} for predictor in predictors}

    for proc, s_str, e_str in procs:
        _tot, _result = proc.get()
        tot += _tot
        for pred in predictors:
            for term in terms:
                result[pred][term] += _result[pred][term]

        if tot == 0:
            continue

        print('Processing video_sent and video_acked data '
              'between {} and {}\n'.format(s_str, e_str))
        print('For tot:', tot)
        for pred in predictors:
            print(pred + ': ' + ', '.join([term + ':{:.5f}'.format(
              result[pred][term] / tot) for term in terms]))

    with open(args.output, 'w') as fh:
        for pred in predictors:
            fh.write(pred + ': ' + ', '.join([term + ':{:.5f}'.format(
                          result[pred][term] / tot) for term in terms]))
            fh.write('\n')

if __name__ == '__main__':
    main()
