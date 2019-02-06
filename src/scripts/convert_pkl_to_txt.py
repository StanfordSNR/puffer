#!/usr/bin/env python3

import pickle
import argparse
import numpy as np
from helpers import get_abr_cc


def save_video_data(video_data_pickle, expt_id_cache):
    with open(video_data_pickle, 'rb') as fh:
        d = pickle.load(fh)

    x = {}
    for session in d:
        expt_id = session[-1]
        expt_config = expt_id_cache[int(expt_id)]
        abr_cc = get_abr_cc(expt_config)

        if session not in x:
            x[session] = {}
            x[session]['ssim'] = []
            x[session]['channel'] = None
            x[session]['abr'] = abr_cc[0]
            x[session]['cc'] = abr_cc[1]

        for video_ts in d[session]:
            x[session]['ssim'].append(d[session][video_ts]['ssim_index'])

            if x[session]['channel'] is None:
                x[session]['channel'] = d[session][video_ts]['channel']

        x[session]['ssim'] = np.mean(x[session]['ssim'])

    # write to file
    f = open('fugu-video-data.txt', 'w')

    for session in x:
        xs = x[session]

        f.write('{} {} {} {}\n'.format(
            xs['abr'], xs['cc'], xs['channel'], xs['ssim']))

    f.close()


def save_buffer_data(buffer_data_pickle, expt_id_cache):
    with open(buffer_data_pickle, 'rb') as fh:
        d = pickle.load(fh)

    x = {}
    for session in d:
        expt_id = session[-1]
        expt_config = expt_id_cache[int(expt_id)]
        abr_cc = get_abr_cc(expt_config)

        if session not in x:
            x[session] = {}
            x[session]['play'] = d[session]['play']
            x[session]['rebuf'] = d[session]['rebuf']
            x[session]['abr'] = abr_cc[0]
            x[session]['cc'] = abr_cc[1]

    # write to file
    f = open('fugu-buffer-data.txt', 'w')

    for session in x:
        xs = x[session]

        f.write('{} {} {} {}\n'.format(
            xs['abr'], xs['cc'], xs['play'], xs['rebuf']))

    f.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('-v', '--video-data-pickle', required=True)
    parser.add_argument('-b', '--buffer-data-pickle', required=True)
    parser.add_argument('-e', '--expt-id-cache', required=True)
    args = parser.parse_args()

    with open(args.expt_id_cache, 'rb') as fh:
        expt_id_cache = pickle.load(fh)

    save_video_data(args.video_data_pickle, expt_id_cache)

    save_buffer_data(args.buffer_data_pickle, expt_id_cache)


if __name__ == '__main__':
    main()
