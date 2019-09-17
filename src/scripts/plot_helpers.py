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

TCP_INFO = ['delivery_rate', 'cwnd', 'in_flight', 'min_rtt', 'rtt']


def discretize_output(raw_out):
    z = np.array(raw_out)

    z = np.floor((z + 0.5 * Model.BIN_SIZE) / Model.BIN_SIZE).astype(int)
    return np.clip(z, 0, Model.BIN_MAX)


def prepare_tcp_info(chunk, tcp_info):
    row = []
    for term in tcp_info:
        if term == 'delivery_rate':
            row += [chunk[term] / PKT_BYTES]
        else:
            row += [chunk[term]]

    return row


def prepare_intput_output(chunks, tcp_info=TCP_INFO):
    row = []
    for chunk in chunks[1:]:
        row = prepare_tcp_info(chunk, tcp_info) +\
              [chunk['size'] / PKT_BYTES, chunk['trans_time']] + row
    row += prepare_tcp_info(chunks[0], tcp_info) +\
           [chunks[0]['size'] / PKT_BYTES]

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


pretty_name = {
    'bbr': 'BBR',
    'cubic': 'Cubic',
    'puffer_ttp_cl': 'Tetra',
    'puffer_ttp_20190402': 'Non-continual Tetra',
    'puffer_ttp_emu': 'Emulation-trained Tetra',
    'linear_bba': 'BBA',
    'mpc': 'MPC-HM',
    'robust_mpc': 'RobustMPC-HM',
    'pensieve': 'Pensieve',
}


pretty_color = {
    'puffer_ttp_cl': 'C3',
    'puffer_ttp_20190402': 'C1',
    'puffer_ttp_emu': 'C6',
    'linear_bba': 'C2',
    'mpc': 'C0',
    'robust_mpc': 'C5',
    'pensieve': 'C4',
}


pretty_linestyle = {
    'puffer_ttp_cl': '-',  # solid
    'puffer_ttp_20190402': '--',  # dashed
    'linear_bba': ':',  # dotted
    'mpc': (0, (5, 1)),  # densely dashed
    'robust_mpc': (0, (3, 1, 1, 1)),  # densely dashdotted
    'pensieve': '-.',  # dashdot
}


abr_order = [
    'puffer_ttp_cl',
    'linear_bba',
    'mpc',
    'robust_mpc',
    'pensieve',
]
