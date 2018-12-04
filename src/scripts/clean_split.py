#!/usr/bin/env python3

import os
from os import path
import argparse
import inotify.adapters

'''
Example usage:
nc <host> <port> |
tee >(split -d -a 5 -b 104857600 --additional-suffix=.ts - XXX-) |
<decoder>
'''


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('dir', help='directory to watch')
    parser.add_argument('max', metavar='N', type=int,
                        help='remove the piece N pieces before the latest one')
    args = parser.parse_args()

    watch_dir = args.dir
    max_pieces = args.max

    i = inotify.adapters.Inotify()
    i.add_watch(watch_dir)

    for event in i.event_gen():
        if event is None:
            continue

        (header, type_names, watch_path, filename) = event

        if 'IN_CLOSE_WRITE' not in type_names:
            continue

        splitted_name = path.basename(filename).split('.')
        if splitted_name[-1] != 'ts':
            continue

        print(filename, 'is closed')
        pre, num = splitted_name[0].split('-')
        if int(num) < max_pieces:
            continue

        old_file = pre + '-' + str(int(num) - max_pieces).zfill(len(num)) + '.ts'
        old_path = path.join(watch_dir, old_file)

        if not path.isfile(old_path):
            print('Cannot remove: {} does not exist'.format(old_path))
            continue

        os.remove(old_path)
        print(old_file, 'is removed')


if __name__ == '__main__':
    main()
