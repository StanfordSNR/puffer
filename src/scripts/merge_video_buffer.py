#!/usr/bin/env python3

import os
from os import path
import sys
import argparse


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--video', required=True)
    parser.add_argument('--buffer', required=True)
    parser.add_argument('-o', required=True)
    args = parser.parse_args()

    if not path.isdir(args.o):
        os.makedirs(args.o)

    for data_fname in os.listdir(args.video):
        if path.splitext(data_fname)[1] != '.csv':
            continue

        # parse video data
        video_data_path = path.join(args.video, data_fname)
        video_data = {}
        with open(video_data_path) as fh:
            for line in fh:
                split_line = line.strip().split(',')
                session = tuple(split_line[:3])

                if session not in video_data:
                    video_data[session] = split_line[3:]
                else:
                    sys.exit('Duplicate session found {} in {}'
                             .format(session, video_data_path))

        # parse buffer data
        buffer_data_path = path.join(args.buffer, data_fname)
        buffer_data = {}
        with open(buffer_data_path) as fh:
            for line in fh:
                split_line = line.strip().split(',')
                session = tuple(split_line[:3])

                if session not in buffer_data:
                    buffer_data[session] = split_line[3:]
                else:
                    sys.exit('Duplicate session found {} in {}'
                             .format(session, buffer_data_path))

        common_sessions = set(video_data) & set(buffer_data)

        # output merged sessions
        output_path = path.join(args.o, data_fname)
        with open(output_path, 'w') as fh:
            # sort sessions by init_id
            for session in sorted(common_sessions, key=lambda s: int(s[1])):
                l = list(session) + video_data[session] + buffer_data[session]
                fh.write(','.join(l) + '\n')


if __name__ == '__main__':
    main()
