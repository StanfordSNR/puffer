#!/usr/bin/env python3

import os
import sys
import requests
import argparse
from os import path
from datetime import datetime, timedelta
from subprocess import check_call


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('yaml_settings')
    args = parser.parse_args()

    curr_dir = path.dirname(path.abspath(__file__))
    plot_src = path.join(curr_dir, 'plot_ssim_rebuffer.py')

    days = 1
    time_str = '%Y-%m-%d'
    curr_ts = datetime.utcnow()
    start_ts = curr_ts - timedelta(days=days)

    time_range = start_ts.strftime(time_str) + '_' + curr_ts.strftime(time_str)
    output_fig_name = time_range + '.png'
    output_fig = path.join(curr_dir, output_fig_name)

    # run plot_ssim_rebuffer.py
    cmd = [plot_src, args.yaml_settings, '-d', str(days), '-o', output_fig]
    sys.stderr.write(' '.join(cmd) + '\n')
    check_call(cmd)

    # upload output_fig to Google cloud storage
    bucket_folder = 'puffer-stanford-public/ssim-rebuffer-figs'
    cmd = 'gsutil cp {} gs://{}'.format(output_fig, bucket_folder)
    sys.stderr.write(cmd + '\n')
    check_call(cmd, shell=True)
    gs_url = ('https://storage.googleapis.com/{}/{}'
              .format(bucket_folder, output_fig_name))

    # remove local output_fig
    os.remove(output_fig)

    # post output_fig to Zulip
    content = ('Performance of the ongoing experimental groups '
               'over the past day:\n' + gs_url)
    payload = [
        ('type', 'stream'),
        ('to', 'puffer-notification'),
        ('subject', 'Daily Report'),
        ('content', content),
    ]
    response = requests.post(
        os.environ['ZULIP_URL'], data=payload,
        auth=(os.environ['ZULIP_BOT_EMAIL'], os.environ['ZULIP_BOT_TOKEN']))
    if response.status_code == requests.codes.ok:
        print('Posted to Zulip successfully')
    else:
        print('Failed to post to Zulip')


if __name__ == '__main__':
    main()
