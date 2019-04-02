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

        #x.append([abr_cc[1], abr_cc[0], d[session]['play']])
        x[abr_cc].append(d[session]['play'])

#    return x

    y = {}

    for abr in x:
        y[abr] = {'tot_duration': np.sum(x[abr]),
                  'median_duration': np.median(x[abr]),
                  'mean_duration': np.mean(x[abr]),
                  'sem_duration': np.std(x[abr]) / np.sqrt(len(x[abr])),
                 }

    return y


def filt_fast(video_data, buffer_data, min_tput):
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
        if sess in buffer_data:
            del buffer_data[sess]
            del_buffer += 1

    print('Number of deleted video_data sessions:', len(del_video_sess))
    print('Number of left video_data sessions:', len(video_data))
    print('Number of deleted buffer_data sessions:', del_buffer)
    print('Number of left buffer_data sessions:', len(buffer_data))


def print_d(d, cc, fp):

    fp.write(pretty_names[cc] + '\n')

    for abr in abr_order:
        if (abr, cc) not in d:
            continue
        fp.write(abr + ': '
                 + str(d[(abr, cc)]['tot_duration'] / 3600) + ','
                 + str(d[(abr, cc)]['median_duration'] / 60) + ','
                 + str(d[(abr, cc)]['mean_duration'] / 60) + ','
                 + str(d[(abr, cc)]['sem_duration'] / 60) + '\n')


def plot(expt_id_cache, args):
    if args.filt_fast:
        filt = '_slow'
    else:
        filt = ''
    pre_dp = args.pre_dp + filt + '.pickle'
    output = args.pre_dp + filt + '.txt'

    if os.path.isfile(pre_dp):
        with open(pre_dp, 'rb') as fp:
            d = pickle.load(fp)
    else:
        with open(args.video_data_pickle, 'rb') as fh:
            video_data = pickle.load(fh)

        with open(args.buffer_data_pickle, 'rb') as fh:
            buffer_data = pickle.load(fh)
            print('Finish loading buffer data!')

       # with open('duration.txt', 'w') as fp:
        #    d = collect_ave_duration(buffer_data, expt_id_cache, args)
         #   for t in d:
          #      fp.write(str(t) + '\n')
#
 #           return

        if args.filt_fast:
            filt_fast(video_data, buffer_data, 6)

        d = collect_ave_duration(buffer_data, expt_id_cache, args)

        with open(pre_dp, 'wb') as fp:
            pickle.dump(d, fp)

    with open(output, 'w') as fp:
        fp.write('Total duration (h), Median duration (min), '
                 'Mean duration (min), SEM duration (min)\n')
        print_d(d, 'bbr', fp)
        fp.write('\n')
        print_d(d, 'cubic', fp)
        sys.stderr.write('Print to {}\n'.format(output))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('-b', '--buffer-data-pickle', required=True)
    parser.add_argument('-v', '--video-data-pickle')
    parser.add_argument('--emu', action='store_true')
    parser.add_argument('-e', '--expt-id-cache',
                        default='expt_id_cache.pickle')
    parser.add_argument('--pre-dp', default='duration')
    parser.add_argument('--filt-fast', action='store_true')

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
