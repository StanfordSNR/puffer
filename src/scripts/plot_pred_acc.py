#!/usr/bin/env python3

import os
import sys
import argparse
import yaml
import json
import numpy as np

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

from helpers import connect_to_influxdb
from stream_processor import VideoStreamCallback
from ttp import Model
from ttp2 import Model as Linear_Model

PKT_BYTES = 1500
VIDEO_DURATION = 180180

args = None
expt = {}
influx_client = None
ttp_model = Model()
linear_model = Linear_Model()
result = {'ttp': 0, 'linear': 0, 'harmonic': 0}
tot = 0


def prepare_intput_output(chunks):
    row = []
    for chunk in chunks[1:]:
        row = [chunk['delivery_rate'] / PKT_BYTES, chunk['cwnd'],
               chunk['in_flight'], chunk['min_rtt'], chunk['rtt'],
               chunk['size'] / PKT_BYTES, chunk['trans_time']] + row
    row += [chunks[0]['delivery_rate'] / PKT_BYTES, chunks[0]['cwnd'],
            chunks[0]['in_flight'], chunks[0]['min_rtt'],
            chunks[0]['rtt'], chunks[0]['size'] / PKT_BYTES]

    return ([row], [chunks[0]['trans_time']])

def model_pred(model, raw_in):
    input_data = model.normalize_input(raw_in, update_obs=False)
    model.set_model_eval()

    return model.predict(input_data)

def harmonic_pred(chunks):
    prev_trans = 0

    for chunk in chunks[1:]:
        prev_trans += chunk['trans_time'] / chunk['size']
    ave_trans = prev_trans / (len(chunks) - 1)

    return [chunks[0]['size'] * ave_trans]

def process_session(s):
    global tot
    global result
    global ttp_model
    global linear_model
    global expt

    past_chunks = ttp_model.PAST_CHUNKS

    for curr_ts in s:
        chunks = []

        for i in reversed(range(past_chunks + 1)):
            ts = curr_ts - i * VIDEO_DURATION
            if ts not in s:
                break
            chunks.append(s[ts])

        if len(chunks) != past_chunks + 1:
            continue

        tot += 1
        (raw_in, raw_out) = prepare_intput_output(chunks)
        output = ttp_model.discretize_output(raw_out)
        # ttp
        raw_ttp_out = model_pred(ttp_model, raw_in)
        ttp_out = ttp_model.discretize_output(raw_ttp_out)

        if ttp_out[0] == output[0]:
            result['ttp'] += 1

        # linear
        raw_linear_out = model_pred(linear_model, raw_in)
        linear_out = ttp_model.discretize_output(raw_linear_out)

        if linear_out[0] == output[0]:
            result['linear'] += 1

        # harmonic
        raw_harm_out = harmonic_pred(chunks)
        harm_out = ttp_model.discretize_output(raw_harm_out)

        if harm_out[0] == output[0]:
            result['harmonic'] += 1

        if tot % 1000 == 0:
            print("For tot:", tot)
            print("ttp pred acc: %5f" % (result['ttp'] / tot))
            print("linear pred acc: %5f" % (result['linear'] / tot))
            print("harmonic pred acc: %5f" % (result['harmonic'] / tot))

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

    video_stream = VideoStreamCallback(process_session)
    video_stream.process(influx_client, args.start_time, args.end_time)

    with open(args.output, 'w') as fh:
        fh.write("Pred Acc: ttp: {:.5f}, linear: {:.5f}, harmonic: {:.5f}"
                .format(result['ttp'] / tot, result['linear'] / tot,
                 result['harmonic'] / tot))

if __name__ == '__main__':
    main()
