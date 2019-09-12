#!/usr/bin/env python3

import os
import sys
import argparse
import yaml
import json
import numpy as np

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plti
from ttp import Model, PKT_BYTES


def discretize_output(raw_out):
    z = np.array(raw_out)

    z = np.floor((z + 0.5 * Model.BIN_SIZE) / Model.BIN_SIZE).astype(int)
    return np.clip(z, 0, Model.BIN_MAX)


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

    return model.predict_distr(input_data)

def distr_bin_pred(distr):
    max_bin = np.argmax(distr, axis=1)
    ret = []
    for bin_id in max_bin:
        if bin_id == 0:  # the first bin is defined differently
            ret.append(0.25 * Model.BIN_SIZE)
        else:
            ret.append(bin_id * Model.BIN_SIZE)

    return ret

def distr_l1_pred(distr):
    ret = []
    for dis in distr:
        cnt = 0
        for i in range(len(dis)):
            cnt += dis[i]
            if cnt > 0.5:
                break

        if i == 0:
            ret.append(0.5 * Model.BIN_SIZE / cnt * 0.5)
        else:
            tmp = 0.5 - cnt + 0.5 * dis[i]
            ret.append((i + tmp / dis[i]) * Model.BIN_SIZE)

    return ret

def distr_l2_pred(distr):
    ret = []
    for dis in distr:
        cnt = 0
        for i in range(len(dis)):
            if i == 0:
                cnt += dis[i] * 0.25 * Model.BIN_SIZE
            else:
                cnt += dis[i] * i * Model.BIN_SIZE

        ret.append(cnt)

    return ret

def bin_acc(y, _y):
    return  discretize_output(y) != discretize_output(_y)

def l1_loss(y, _y):
    return np.abs(y - _y)

def cut_acc(y, _y):
    return np.abs(y - _y) > Model.BIN_SIZE * 0.5

def l2_loss(y, _y):
    return (y - _y) * (y - _y)

def harmonic_pred(chunks):
    prev_trans = 0

    for chunk in chunks[1:]:
        prev_trans += chunk['trans_time'] / chunk['size']
    ave_trans = prev_trans / (len(chunks) - 1)

    return [chunks[0]['size'] * ave_trans]
