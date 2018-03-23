#!/usr/bin/env python3

import sys
import errno
import argparse
from time import sleep
from tempfile import mkstemp
from shutil import copyfile, move, rmtree
from os import path, listdir, makedirs
from test_helpers import make_sure_path_exists, copy_move


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('src')
    parser.add_argument('dst')
    args = parser.parse_args()

    src_ready = path.join(args.src, 'ready')
    if not path.isdir(src_ready):
        sys.exit(1)

    dst_ready = path.join(args.dst, 'ready')
    if path.isdir(dst_ready):
        rmtree(dst_ready)
        sys.stderr.write('Removed %s\n' % dst_ready)

    make_sure_path_exists(dst_ready)
    for d in listdir(src_ready):
        dst_dir = path.join(dst_ready, d)
        make_sure_path_exists(dst_dir)
    sys.stderr.write('Created directores\n')

    user_input = input("Input 'y' to continue: ")
    if user_input != 'y':
        sys.stderr.write('Wrong input\n')
        return

    fm = {}
    idx = {}

    for d in listdir(src_ready):
        fm[d] = []
        idx[d] = 0

        src_dir = path.join(src_ready, d)
        dst_dir = path.join(dst_ready, d)
        make_sure_path_exists(dst_dir)

        for f in listdir(src_dir):
            if 'init' in f:
                copy_move(path.join(src_dir, f), path.join(dst_dir, f))
            else:
                ts = int(path.splitext(f)[0])
                fm[d].append((ts, f))

        fm[d].sort(key=lambda tup : tup[0])


    while True:
        sleep(2)
        empty = True

        for d in fm:
            if idx[d] < len(fm[d]):
                empty = False
                ts, f = fm[d][idx[d]]

                print(path.join(src_ready, d, f) + ' -> ' +
                      path.join(dst_ready, d, f))

                copy_move(path.join(src_ready, d, f),
                          path.join(dst_ready, d, f))
                idx[d] += 1

        if empty:
            break


if __name__ == '__main__':
    main()
