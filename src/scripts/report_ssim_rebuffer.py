#!/usr/bin/env python3

import os
import sys
import requests
import argparse
from os import path
from datetime import datetime, timedelta
from subprocess import check_call


time_str = '%Y-%m-%dT%H:%M:%SZ'
short_time_str = '%Y-%m-%d'
args = None


def report_ssim_rebuffer(curr_ts, days):
    start_ts = curr_ts - timedelta(days=days)

    curr_dir = path.dirname(path.abspath(__file__))
    plot_src = path.join(curr_dir, 'plot_ssim_rebuffer.py')

    time_range = '{}_{}'.format(start_ts.strftime(short_time_str),
                                curr_ts.strftime(short_time_str))
    output_fig_name = time_range + '.png'
    output_fig = path.join(curr_dir, output_fig_name)

    # run plot_ssim_rebuffer.py
    cmd = [plot_src, args.yaml_settings, '-o', output_fig,
           '--from', start_ts.strftime(time_str),
           '--to', curr_ts.strftime(time_str)]
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
    if days == 1:
        content = ('Performance of ongoing experiments '
                   'over the past day:\n' + gs_url)
    elif days == 7:
        content = ('Performance of ongoing experiments '
                   'over the past week:\n' + gs_url)
    else:
        content = ('Performance of ongoing experiments '
                   'over the past {} days: {}\n').format(days, gs_url)

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


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('yaml_settings')
    global args
    args = parser.parse_args()

    td = datetime.utcnow()
    curr_ts = datetime(td.year, td.month, td.day, td.hour, 0)

    # report the performance over the past day
    report_ssim_rebuffer(curr_ts, 1)

    # report the performance over the past week
    report_ssim_rebuffer(curr_ts, 7)


if __name__ == '__main__':
    main()
