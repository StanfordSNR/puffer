#!/usr/bin/env python3

import argparse
import random
import math

from helpers import datetime_iter


backup_hour = 11  # back up at 11 AM (UTC) every day
args = None


PKT_BYTES = 1500
TRACES_PER_DAY = 40
TRACE_LEN = 320000  # ms


def gen_mm_trace(s_str, e_str):
    csv_fname = 'raw_trace_{}.csv'.format(s_str[:-7])
    csv_fh = open(csv_fname)

    data = {}
    for line in csv_fh:
        epoch_ts, session_id, chunk_size, trans_time, min_rtt = line.split(',')
        epoch_ts = int(epoch_ts)
        chunk_size = float(chunk_size) / PKT_BYTES  # packets
        trans_time = float(trans_time)  # ms
        min_rtt = float(min_rtt)  # ms

        pkts_per_ms = chunk_size / (trans_time - min_rtt / 2)  # pkts / ms

        if session_id not in data:
            data[session_id] = {}
            data[session_id]['ts'] = []
            data[session_id]['tput'] = []
            data[session_id]['min_rtt'] = float('inf')

        data[session_id]['ts'].append(epoch_ts)
        data[session_id]['tput'].append(pkts_per_ms)
        data[session_id]['min_rtt'] = min(data[session_id]['min_rtt'], min_rtt)

    sampled_session_ids = set()
    data_keys = list(data.keys())
    while len(sampled_session_ids) < TRACES_PER_DAY:
        session_id = random.choice(data_keys)
        if session_id in sampled_session_ids:
            continue

        # must be longer than TRACE_LEN seconds
        if data[session_id]['ts'][-1] - data[session_id]['ts'][0] <= TRACE_LEN:
            continue

        sampled_session_ids.add(session_id)

        # output Mahimahi trace
        mm_delay = int(round(data[session_id]['min_rtt'] / 2))
        safe_session_id = session_id.replace('/', '_').replace('+', '-')
        fh = open('{}mmdelay{}.trace'.format(safe_session_id, mm_delay), 'w')

        total_points = len(data[session_id]['ts'])
        assert(total_points == len(data[session_id]['tput']))

        curr = 1
        mm_global_ts = 0

        while curr < total_points and mm_global_ts < TRACE_LEN:
            prev_ts = data[session_id]['ts'][curr - 1]
            pkts_per_ms = data[session_id]['tput'][curr - 1]
            curr_ts = data[session_id]['ts'][curr]

            pkt_sent = 0
            for ts in range(prev_ts, curr_ts):
                mm_global_ts += 1

                target_pkt_sent = int((ts - prev_ts + 1) * pkts_per_ms)
                if target_pkt_sent > pkt_sent:
                    for _ in range(target_pkt_sent - pkt_sent):
                        fh.write('{}\n'.format(mm_global_ts))
                    pkt_sent = target_pkt_sent

                if mm_global_ts >= TRACE_LEN:
                    break

            curr += 1

        fh.close()


    csv_fh.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--from', required=True, dest='start_date',
                        help='e.g., "2019-04-03" ({} AM in UTC)'.format(backup_hour))
    parser.add_argument('--to', required=True, dest='end_date',
                        help='e.g., "2019-04-05" ({} AM in UTC)'.format(backup_hour))
    global args
    args = parser.parse_args()

    # parse input dates
    start_time_str = args.start_date + 'T{}:00:00Z'.format(backup_hour)
    end_time_str = args.end_date + 'T{}:00:00Z'.format(backup_hour)

    for s_str, e_str in datetime_iter(start_time_str, end_time_str):
        gen_mm_trace(s_str, e_str)


if __name__ == '__main__':
    main()
