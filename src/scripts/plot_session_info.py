#!/usr/bin/env python3

import sys
import argparse
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt


def plot_session_duration_and_throughput(d, time_start, time_end):
    session_durations = []
    tputs = []
    total_rebuffer = []
    rebuffer_percent = []
    for session in d:
        ds = d[session]
        if len(ds) < 5:
            # Dont count sessions that dont deliver 10 seconds of video
            continue
        session_durations.append(len(ds) * 2.002 / 60)  # minutes
        tput_sum = 0
        tput_count = 0
        first_ts = True
        second_ts = True
        for next_ts in sorted(ds.keys()):  # Need to iterate in order!
            if 'trans_time' not in ds[next_ts]:
                continue
            if first_ts:
                first_ts = False
            elif second_ts:  # TODO: Better method for detecting startup delay
                startup_delay = ds[next_ts]['cum_rebuffer']
                second_ts = False
            tput_sum += (ds[next_ts]['size'] * 8 / ds[next_ts]['trans_time'] /
                         1000000)  # Mbps
            tput_count += 1
        if tput_count == 0:
            continue
        if not first_ts:
            # Rebuffer time = cum rebuffer of last chunk - startup delay
            total_rebuffer.append(ds[sorted(ds.keys())[-1]]['cum_rebuffer'] -
                                  startup_delay)
            rebuffer_percent.append(total_rebuffer[-1] / (session_durations[-1] + total_rebuffer[-1]) * 100)
        tputs.append(tput_sum/tput_count)  # Average tput for session

    fig, ax = plt.subplots()
    ax.hist(session_durations, bins=100000, normed=1, cumulative=True,
            histtype='step')
    ax.grid(True)
    ax.set_xlabel('Session duration (minutes)')
    ax.set_ylabel('CDF')
    title = ('Session Duration from [{}, {}] (UTC)'
             .format(time_start, time_end))
    xmin, xmax = ax.get_xlim()
    xmax = 50
    ax.set_xlim(0, xmax)
    ax.set_title(title)
    fig.savefig('session_duration.png', dpi=150, bbox_inches='tight',
                pad_inches=0.2)
    sys.stderr.write('Saved session duration plot to {}\n'
                     .format('session_duration.png'))

    fig, ax = plt.subplots()
    ax.hist(tputs, bins=100000, normed=1, cumulative=True,
            histtype='step')
    ax.grid(True)
    ax.set_xlabel('Session throughput (Mbps)')
    ax.set_ylabel('CDF')
    title = ('Session Throughputs from [{}, {}] (UTC)'
             .format(time_start, time_end))
    xmin, xmax = ax.get_xlim()
    xmax = 100
    ax.set_xlim(0, xmax)
    ax.set_title(title)
    fig.savefig('session_throughputs.png', dpi=150, bbox_inches='tight',
                pad_inches=0.2)
    sys.stderr.write('Saved session throughputs plot to {}\n'
                     .format('session_throughputs.png'))

    fig, ax = plt.subplots()
    ax.hist(rebuffer_percent, bins=100000, normed=1, cumulative=True,
            histtype='step')
    ax.grid(True)
    ax.set_xlabel('% Rebuffer (excluding startup) - all ABR')
    ax.set_ylabel('CDF')
    title = ('Rebuffer % (all ABR) from [{}, {}) \
             (UTC)'.format(time_start, time_end))
    xmin, xmax = ax.get_xlim()
    # xmax = 100
    ax.set_xlim(0, xmax)
    ax.set_title(title)
    fig.savefig('rebuffer_percent.png', dpi=150, bbox_inches='tight',
                pad_inches=0.2)
    sys.stderr.write('Saved rebuffer percent plot to {}\n'
                     .format('rebuffer_percent.png'))

    print('Percentage of all sessions with a rebuffer: ' +
          str(np.count_nonzero(total_rebuffer) / len(session_durations)))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('yaml_settings')
    parser.add_argument('--from', dest='time_start',
                        help='datetime in UTC conforming to RFC3339')
    parser.add_argument('--to', dest='time_end',
                        help='datetime in UTC conforming to RFC3339')
    args = parser.parse_args()

    #FIXME
    #if args.session_info:
    #    plot_session_duration_and_throughput(raw_data, args.time_start,
    #                                         args.time_end)


if __name__ == '__main__':
    main()
