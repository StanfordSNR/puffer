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


def collect_buffer_data(d, expt_id_cache, args):
    init_id_flag = {sess[:2]: None for sess in d}
    x = {}

    num_del = 0

    for session in d:
        expt_id = session[-1]

        user, init_id = session[:2]

        if args.filt_mode == 'filt_fake':
            if (user, init_id - 1) in init_id_flag:
                num_del += 1
                continue
        elif args.filt_mode == 'filt_real':
            if (user, init_id - 1) not in init_id_flag:
                num_del += 1
                continue

        if not args.emu:
            expt_config = expt_id_cache[int(expt_id)]
            abr_cc = get_abr_cc(expt_config)
        else:
            abr_cc = tuple(expt_id.split('+'))

        if abr_cc not in x:
            x[abr_cc] = []

        x[abr_cc].append(d[session]['startup'])

    print("Num of delete:", num_del)

    y = {}

    for abr in x:
        y[abr] = {'buffer_num': len(x[abr]),
                  'startup_mean': np.mean(x[abr])}

    return y


def collect_video_data(d, expt_id_cache, args):
    init_id_flag = {sess[:2]: None for sess in d}
    num_real_startup = 0
    x = {}

    num_del = 0

    for session in d:
        expt_id = session[-1]

        user, init_id = session[:2]

        if args.filt_mode == 'filt_fake':
            if (user, init_id - 1) in init_id_flag:
                num_del += 1
                continue
        elif args.filt_mode == 'filt_real':
            if (user, init_id - 1) not in init_id_flag:
                num_del += 1
                continue

        if not args.emu:
            expt_config = expt_id_cache[int(expt_id)]
            abr_cc = get_abr_cc(expt_config)
        else:
            abr_cc = tuple(expt_id.split('+'))

        if abr_cc not in x:
            x[abr_cc] = []

        start_ts = min(d[session].keys())
        x[abr_cc].append(d[session][start_ts]['ssim_index'])

    print("Num of delete:", num_del)

    y = {}

    for abr in x:
        y[abr] = {'video_num': len(x[abr]),
                  'ssim_mean': ssim_index_to_db(np.mean(x[abr])),
                 }

    return y


def combine_by_cc(d1, d2):
    x = {}
    for abr in d1:
        x[abr] = d1[abr]
        x[abr].update(d2[abr])

    return x


def pick_by_cc(d, tag):
    return {abr:d[abr][tag] for abr in d}


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


def plot_dots(x_mean, y_mean, args):
    xmin = 0
    xmax = 0.85
    ymin = 15.1
    ymax = 17.1

    for cc in ['bbr', 'cubic']:
        fig, ax = plt.subplots()

        if args.filt_mode == 'no_filt':
            ax.set_xlabel('Start-up Delay (Sec)')
        elif args.filt_mode == 'filt_fake':
            ax.set_xlabel('Initial Video Delay (Sec)')
        elif args.filt_mode == 'filt_real':
            ax.set_xlabel('Video Swich Delay (Sec)')
        ax.set_ylabel('Average SSIM (dB)')

        # Hide the right and top spines
        ax.spines['right'].set_visible(False)
        ax.spines['top'].set_visible(False)

        for abr_cc in y_mean:
            if abr_cc[1] != cc:
                continue
            abr = abr_cc[0]

            x = x_mean[abr_cc]  # Sec
            y = y_mean[abr_cc]  # dB
            print(abr_cc, x, y)

            ax.scatter(x, y, color=pretty_colors[abr])
            # error bars are too small
            # ax.errorbar(x, y, yerr=[[sem[0]], [sem[1]]])
            ax.annotate(pretty_names[abr], (x, y))

        #ax.set_xlim(xmin, xmax)
        #ax.set_ylim(ymin, ymax)

        ax.invert_xaxis()

        if args.filt_mode == 'no_filt':
            fig_name = '{}_ssim_startup'.format(cc)
        elif args.filt_mode == 'filt_fake':
            fig_name = '{}_ssim_initial_delay'.format(cc)
        elif args.filt_mode == 'filt_real':
            fig_name = '{}_ssim_switch_delay'.format(cc)
        fig.savefig(fig_name + '.png')
        sys.stderr.write('Saved plot to {}\n'.format(fig_name + '.png'))


def plot(expt_id_cache, args):
    pre_dp = args.pre_dp + '_' + args.filt_mode + '.pickle'


    if os.path.isfile(pre_dp):
        with open(pre_dp, 'rb') as fp:
            d = pickle.load(fp)
    else:
        with open(args.video_data_pickle, 'rb') as fh:
            video_data = pickle.load(fh)
            print('Finish loading video data!')

        with open(args.buffer_data_pickle, 'rb') as fh:
            buffer_data = pickle.load(fh)
            print('Finish loading buffer data!')

        bd = collect_buffer_data(buffer_data, expt_id_cache, args)
        vd = collect_video_data(video_data, expt_id_cache, args)
        d = combine_by_cc(bd, vd)

        with open(pre_dp, 'wb') as fp:
            pickle.dump(d, fp)

    if args.filt_mode == 'no_filt':
        output = 'all_startup'
    elif args.filt_mode == 'filt_fake':
        output = 'real_startup'
    elif args.filt_mode == 'filt_real':
        output = 'fake_startup'

    with open(output + '.txt', 'w') as fp:
        for abr in d:
            fp.write(str(list(d[abr].keys())))
            break
        fp.write('\n')
        print_d(d, 'bbr', fp)
        fp.write('\n')
        print_d(d, 'cubic', fp)
        sys.stderr.write('Print to {}\n'.format(output + '.txt'))

        plot_dots(pick_by_cc(d, 'startup_mean'),
                  pick_by_cc(d, 'ssim_mean'),
                  args)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('-b', '--buffer-data-pickle', required=True)
    parser.add_argument('-v', '--video-data-pickle', required=True)
    parser.add_argument('--emu', action='store_true')
    parser.add_argument('-e', '--expt-id-cache',
                        default='expt_id_cache.pickle')
    parser.add_argument('--pre-dp', default='startup')
    parser.add_argument('--filt-mode', default='no_filt')
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
