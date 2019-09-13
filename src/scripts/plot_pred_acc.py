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

from helpers import connect_to_influxdb
from stream_processor import VideoStream
from ttp import Model, PKT_BYTES
from ttp2 import Model as Linear_Model
from plot_helpers import *


VIDEO_DURATION = 180180
MID_SIZE = 766929
PAST_CHUNKS = 8

args = None
expt = {}
influx_client = None
ttp_model = Model()
linear_model = Linear_Model()
terms = ['bin', 'l1', 'l2']
predictors = ['ttp', 'ttp_mle', 'ttp_tp', 'linear', 'tcp_info', 'harmonic']
result = {predictor: {term: 0 for term in terms} for predictor in predictors}

tot = 0


def process_session(session, s):
    global tot
    global result
    global ttp_model
    global linear_model
    global expt
    global terms
    global predictors
    global PAST_CHUNKS

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

        tot += 1
        (raw_in, raw_out) = prepare_intput_output(chunks)
        unit_raw_in = copy.deepcopy(raw_in)
        unit_raw_in[0][-1] = MID_SIZE / PKT_BYTES

        ttp_distr = model_pred(ttp_model, raw_in)

        linear_distr = model_pred(linear_model, raw_in)

        ttp_tp_distr = model_pred(ttp_model, unit_raw_in)
        bin_ttp_tp_out = distr_bin_pred(ttp_tp_distr)
        bin_ttp_tp_out[0] *= curr_chunk_size / MID_SIZE

        tcp_info_out = curr_chunk_size / chunks[0]['delivery_rate']

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

        # harmonic
        harm_out = harmonic_pred(chunks)

        result['harmonic']['bin'] += bin_acc(harm_out[0], raw_out[0])
        result['harmonic']['l1'] += l1_loss(harm_out[0], raw_out[0])
        result['harmonic']['l2'] += l2_loss(harm_out[0], raw_out[0])

        if tot % 1000 == 0:
            print('For tot:', tot)
            for pred in predictors:
                print(pred + ': ' + ', '.join([term + ':{:.5f}'.format(
                          result[pred][term] / tot) for term in terms]))

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('yaml_settings')
    parser.add_argument('--from', dest='start_time', required=True,
                        help='datetime in UTC conforming to RFC3339')
    parser.add_argument('--to', dest='end_time', required=True,
                        help='datetime in UTC conforming to RFC3339')
    parser.add_argument('--expt', help='e.g., expt_cache.json')
    parser.add_argument('-o', '--output', required=True)
    parser.add_argument('--ttp-path', dest='ttp_path', required=True)
    global args
    args = parser.parse_args()

    with open(args.yaml_settings, 'r') as fh:
        yaml_settings = yaml.safe_load(fh)

    with open(args.expt, 'r') as fh:
        global expt
        expt = json.load(fh)

    # create an InfluxDB client and perform queries
    global influx_client
    influx_client = connect_to_influxdb(yaml_settings)

    global ttp_model
    ttp_model.load(args.ttp_path)

    global linear_model
    linear_model.load("/home/ubuntu/models/puffer_ttp/linear/py-0.pt")

    video_stream = VideoStream(process_session)
    video_stream.process(influx_client, args.start_time, args.end_time)

    with open(args.output, 'w') as fh:
        for pred in predictors:
            fh.write(pred + ': ' + ', '.join([term + ':{:.5f}'.format(
                          result[pred][term] / tot) for term in terms]))
            fh.write('\n')

if __name__ == '__main__':
    main()
