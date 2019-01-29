#!/usr/bin/env python3

import os
import sys
import yaml
import json
import pickle
import argparse
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

from helpers import (
    connect_to_postgres, ssim_index_to_db, retrieve_expt_config, get_abr_cc,
    pretty_names, pretty_colors, abr_order)
from collect_data import VIDEO_DURATION



def collect_data(d, expt_id_cache, args):
    x = {}
    for session in d:
        expt_id = session[-1]

        if not args.emu:
            expt_config = expt_id_cache[int(expt_id)]
            abr_cc = get_abr_cc(expt_config)
        else:
            abr_cc = tuple(expt_id.split('+'))

        if abr_cc not in x:
            x[abr_cc] = []

        x[abr_cc].append(d[session]['num_rebuf'])

    y = {}

    for abr in x:
        y[abr] = {'mean': np.mean(x[abr]),
                  'pt_90': np.percentile(x[abr], 90),
                  'pt_93': np.percentile(x[abr], 93),
                  'pt_95': np.percentile(x[abr], 95),
                  'pt_97': np.percentile(x[abr], 97),
                  'pt_98': np.percentile(x[abr], 98),
                  'pt_99': np.percentile(x[abr], 99),
                 }

    return y


def print_d(d, cc, fp):

    fp.write(pretty_names[cc] + '\n')

    for abr in abr_order:
        if (abr, cc) not in d:
            continue
        fp.write(abr + ':')
        ds = d[(abr, cc)]
        for k in ds:
            fp.write(' ' + str(ds[k]) + ',')
        fp.write('\n')


def plot(expt_id_cache, args):

    if args.pre_dp != None and os.path.isfile(args.pre_dp):
        with open(args.pre_dp, 'rb') as fp:
            d = pickle.load(fp)
    else:
        with open(args.buffer_data_pickle, 'rb') as fh:
            buffer_data = pickle.load(fh)
            print('Finish loading buffer data!')

        d = collect_data(buffer_data, expt_id_cache, args)

        with open(args.pre_dp, 'wb') as fp:
            pickle.dump(d, fp)

    output = 'num_rebuf'
    with open(output + '.txt', 'w') as fp:
        for abr in d:
            fp.write(str(list(d[abr].keys())))
            break
        fp.write('\n')
        print_d(d, 'bbr', fp)
        fp.write('\n')
        print_d(d, 'cubic', fp)
        sys.stderr.write('Print to {}\n'.format(output + '.txt'))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('-p', '--buffer-data-pickle', required=True)
    parser.add_argument('--emu', action='store_true')
    parser.add_argument('-e', '--expt-id-cache',
                        default='expt_id_cache.pickle')
    parser.add_argument('--pre-dp', default='num_rebuf.pickle')

    args = parser.parse_args()

    if not args.emu:
        with open(args.expt_id_cache, 'rb') as fh:
            expt_id_cache = pickle.load(fh)

        plot(expt_id_cache, args)
    else:
        # emulation
        plot(None, args)


if __name__ == '__main__':
    main()
