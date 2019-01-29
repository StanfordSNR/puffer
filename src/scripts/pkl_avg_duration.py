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



def collect_ave_duration(d, expt_id_cache, args):
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

        x[abr_cc].append(d[session]['play'])

    ad_mean = {}
    ad_median = {}

    for abr in x:
        ad_mean[abr] = np.mean(x[abr])
        ad_median[abr] = np.median(x[abr])

    return ad_mean, ad_median


def print_ad(ad_mean, ad_median, cc, fp):

    fp.write(pretty_names[cc] + '\n')

    for abr in abr_order:
        if (abr, cc) not in ad_mean:
            continue
        fp.write(abr + ': %.3f, %.3f\n' % \
                    (ad_mean[(abr, cc)] / 60, ad_median[(abr, cc)]))


def plot(expt_id_cache, args):

    if args.pre_dp != None and os.path.isfile(args.pre_dp):
        with open(args.pre_dp, 'rb') as fp:
            ad_mean, ad_median = pickle.load(fp)
    else:
        with open(args.buffer_data_pickle, 'rb') as fh:
            buffer_data = pickle.load(fh)
            print('Finish loading buffer data!')

        ad_mean, ad_median = collect_ave_duration(
                buffer_data, expt_id_cache, args)

        with open('avg_duration.pickle', 'wb') as fp:
            pickle.dump((ad_mean, ad_median), fp)

    with open('avg_duration.txt', 'w') as fp:
        fp.write('Mean (Min), Median (Sec)\n')
        print_ad(ad_mean, ad_median, 'bbr', fp)
        fp.write('\n')
        print_ad(ad_mean, ad_median, 'cubic', fp)
        sys.stderr.write('Print to {}\n'.format('avg_duration.txt'))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('-p', '--buffer-data-pickle', required=True)
    parser.add_argument('--emu', action='store_true')
    parser.add_argument('-e', '--expt-id-cache',
                        default='expt_id_cache.pickle')
    parser.add_argument('--pre-dp', default='ave_duration.pickle')

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
