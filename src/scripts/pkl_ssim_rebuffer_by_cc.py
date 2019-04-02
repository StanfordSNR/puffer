#!/usr/bin/env python3

import os
import sys
import pickle
import argparse
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

from helpers import (
    ssim_index_to_db, get_abr_cc, pretty_names, pretty_colors, abr_order)
from collect_data import VIDEO_DURATION


def collect_video_data(d, expt_id_cache, args):
    x = {}

    for session in d:
        expt_id = session[-1]

        # exclude contaminated experiments
        if int(expt_id) == 343 or int(expt_id) == 344:
            continue

        if not args.emu:
            expt_config = expt_id_cache[int(expt_id)]
            abr_cc = get_abr_cc(expt_config)
        else:
            abr_cc = tuple(expt_id.split('+'))

        if abr_cc not in x:
            x[abr_cc] = []

        for video_ts in d[session]:
            # append SSIM index
            x[abr_cc].append(d[session][video_ts]['ssim_index'])

    y = {}

    for abr_cc in x:
        y[abr_cc] = {'ssim_mean': ssim_index_to_db(np.mean(x[abr_cc]))}

        #ssim_index_mean = np.mean(x[abr_cc])
        #sem = np.std(ssim[abr_cc]) / np.sqrt(len(ssim[abr_cc]))

        #ssim_db_lower = ssim_index_to_db(ssim_index_mean - sem)
        #ssim_db_upper = ssim_index_to_db(ssim_index_mean + sem)

        #ssim_db_mean = ssim_index_to_db(ssim_index_mean)
        #ssim_mean[abr_cc] = ssim_db_mean
        #ssim_sem[abr_cc] = (ssim_db_mean - ssim_db_lower,
        #                    ssim_db_upper - ssim_db_mean)

    return y


def collect_buffer_data(d, expt_id_cache, args):
    total_play = {}
    total_rebuf = {}

    for session in d:
        expt_id = session[-1]

        # exclude contaminated experiments
        if int(expt_id) == 343 or int(expt_id) == 344:
            continue

        if not args.emu:
            expt_config = expt_id_cache[int(expt_id)]
            abr_cc = get_abr_cc(expt_config)
        else:
            abr_cc = tuple(expt_id.split('+'))

        if abr_cc not in total_play:
            total_play[abr_cc] = 0
            total_rebuf[abr_cc] = 0

        total_play[abr_cc] += d[session]['play']
        total_rebuf[abr_cc] += d[session]['rebuf']

    y = {}

    for abr_cc in total_play:
        y[abr_cc] = {'rebuf_mean': total_rebuf[abr_cc] / total_play[abr_cc]}

    return y


def filter_fast(video_data, buffer_data, min_tput):
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


def plot_ssim_mean_vs_rebuf_rate(x_mean, y_mean, args):

    # x for rebuf_rate
    # y for ssim

    if args.filter_fast:
        xmin = 0
        xmax = 6
        ymin = 12.5
        ymax = 16
    else:
        xmin = 0
        xmax = 0.85
        ymin = 15.1
        ymax = 17.1

    for cc in ['bbr', 'cubic']:
        fig, ax = plt.subplots()

        ax.set_xlabel('Time spent stalled (%)')
        ax.set_ylabel('Average SSIM (dB)')

        # Hide the right and top spines
        ax.spines['right'].set_visible(False)
        ax.spines['top'].set_visible(False)

        for abr_cc in x_mean:
            if abr_cc[1] != cc:
                continue
            abr = abr_cc[0]

            x = x_mean[abr_cc] * 100  # %
            y = y_mean[abr_cc]  # dB

            ax.scatter(x, y, color=pretty_colors[abr])
            # error bars are too small
            # ax.errorbar(x, y, yerr=[[sem[0]], [sem[1]]])
            ax.annotate(pretty_names[abr], (x, y))

        ax.set_xlim(xmin, xmax)
        ax.set_ylim(ymin, ymax)

        ax.invert_xaxis()

        if args.filter_fast:
            fig_name = '{}_ssim_rebuffer_slow'.format(cc)
        else:
            fig_name = '{}_ssim_rebuffer'.format(cc)
        fig.savefig(fig_name + '.svg')
        sys.stderr.write('Saved plot to {}\n'.format(fig_name + '.svg'))


def plot_compare_dots(x_mean, y_mean, args):

    # x for rebuf_rate
    # y for ssim

    if args.filter_fast:
        xmin = 0
        xmax = 6
        ymin = 12.5
        ymax = 16
    else:
        xmin = 0
        xmax = 0.85
        ymin = 15.1
        ymax = 17.1

    fig, ax = plt.subplots()

    ax.set_xlabel('Time spent stalled (%)')
    ax.set_ylabel('Average SSIM (dB)')

    # Hide the right and top spines
    ax.spines['right'].set_visible(False)
    ax.spines['top'].set_visible(False)

    for abr in abr_order:
        if not ((abr, 'bbr') in x_mean and (abr, 'cubic') in x_mean and
           (abr, 'bbr') in y_mean and (abr, 'cubic') in y_mean):
                continue

        abr_cc = (abr, 'cubic')
        abr_cc_ = (abr, 'bbr')

        x = x_mean[abr_cc] * 100  # %
        y = y_mean[abr_cc]  # dB
        x_ = x_mean[abr_cc_] * 100  # %
        y_ = y_mean[abr_cc_]  # dB

        ax.arrow(x, y, x_ - x, y_ - y, color=pretty_colors[abr],
                 length_includes_head=True, head_starts_at_zero=True,
                 width=0.01 * (xmax - xmin))
        #ax.plot([x, x_], [y, y_], color=pretty_colors[abr], linestyle='-')
        ax.annotate(pretty_names[abr], (x, y))
        #ax.annotate(pretty_names[abr], xy=(x_, y_), xytext=(x, y),
                    #arrowprops=dict(arrowstyle="->"), color=pretty_colors[abr])

    ax.set_xlim(xmin, xmax)
    ax.set_ylim(ymin, ymax)

    ax.invert_xaxis()

    if args.filter_fast:
        fig_name = 'compare_cc_slow'
    else:
        fig_name = 'compare_cc'
    fig.savefig(fig_name + '.svg')
    sys.stderr.write('Saved plot to {}\n'.format(fig_name + '.svg'))



def combine_by_cc(d1, d2):
    x = {}
    for abr in d1:
        x[abr] = d1[abr]
        x[abr].update(d2[abr])

    return x


def pick_by_cc(d, tag):
    return {abr:d[abr][tag] for abr in d}


def print_d(d, output):
    fp = open(output, 'w')

    for abr in d:
        fp.write(str(list(d[abr].keys())) + '\n')
        break

    for cc in ['bbr', 'cubic']:
        fp.write('\n')
        fp.write(pretty_names[cc] + '\n')
        for abr in abr_order:
            if (abr, cc) not in d:
                continue
            fp.write(abr + ':')
            ds = d[(abr, cc)]
            for k in ds:
                fp.write(' ' + str(ds[k]) + ',')
            fp.write('\n')

    sys.stderr.write('Print to {}\n'.format(output))


def plot(expt_id_cache, args):
    if args.filter_fast:
        output_base = args.output_base + '_slow.pickle'
    else:
        output_base = args.output_base + '.pickle'

    if os.path.isfile(output_base):
        with open(output_base, 'rb') as fp:
            d = pickle.load(fp)
    else:
        with open(args.video_data_pickle, 'rb') as fh:
            video_data = pickle.load(fh)

        with open(args.buffer_data_pickle, 'rb') as fh:
            buffer_data = pickle.load(fh)

        sys.stderr.write('Finish loading data\n')

        if args.filter_fast:
            filter_fast(video_data, buffer_data, 6)

        dv = collect_video_data(video_data, expt_id_cache, args)
        db = collect_buffer_data(buffer_data, expt_id_cache, args)
        d = combine_by_cc(dv, db)

        with open(output_base, 'wb') as fp:
            pickle.dump(d, fp)

    print_d(d, '{}.txt'.format(args.output_base))

    if args.task == 'dots':
        plot_ssim_mean_vs_rebuf_rate(pick_by_cc(d, 'rebuf_mean'),
                pick_by_cc(d, 'ssim_mean'), args)
    elif args.task == 'compare':
        plot_compare_dots(pick_by_cc(d, 'rebuf_mean'),
                       pick_by_cc(d, 'ssim_mean'), args)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('-v', '--video-data-pickle', required=True)
    parser.add_argument('-b', '--buffer-data-pickle', required=True)
    parser.add_argument('-e', '--expt-id-cache',
                        default='expt_id_cache.pickle')
    parser.add_argument('--emu', action='store_true')
    parser.add_argument('--filter-fast', action='store_true',
        help='remove sessions with average tput higher than 6 Mbps')
    parser.add_argument('--output-base', default='ssim_rebuffer',
        help='basename of output files (default: ssim_rebuffer)')
    parser.add_argument('-t', '--task', default='dots')
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
