#!/usr/bin/env python3

import sys
import errno
import argparse
import time
from shutil import rmtree
from os import path, listdir
from test_helpers import make_sure_path_exists, copy_move


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('src', help='existing media files')
    parser.add_argument('dst', help='directory to output fake live files')
    parser.add_argument('--video-duration', type=float, default=2.0,
                        metavar='T', help='duration of every video chunk')
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

    user_input = input("Input 'y' to continue after launching media server: ")
    if user_input != 'y':
        sys.stderr.write('Wrong input\n')
        return

    vfm = {}  # video file map
    afm = {}  # video file map
    vidx = {}  # video index
    aidx = {}  # audio index

    for d in listdir(src_ready):
        is_video = True
        if 'k' in d:
            is_video = False

        if is_video:
            vfm[d] = []
            vidx[d] = 0
        else:
            afm[d] = []
            aidx[d] = 0

        src_dir = path.join(src_ready, d)
        dst_dir = path.join(dst_ready, d)
        make_sure_path_exists(dst_dir)

        for f in listdir(src_dir):
            if 'init' in f:
                copy_move(path.join(src_dir, f), path.join(dst_dir, f))
            else:
                ts = int(path.splitext(f)[0])

                if is_video:
                    vfm[d].append((ts, f))
                else:
                    afm[d].append((ts, f))

        if is_video:
            vfm[d].sort(key=lambda tup : tup[0])
        else:
            afm[d].sort(key=lambda tup : tup[0])

    while True:
        no_video_left = True
        same_vts = -1

        for d in vfm:
            if vidx[d] < len(vfm[d]):
                no_video_left = False
                ts, f = vfm[d][vidx[d]]

                if same_vts == -1:
                    same_vts = ts
                assert same_vts == ts

                print(path.join(src_ready, d, f) + ' -> ' +
                      path.join(dst_ready, d, f))

                copy_move(path.join(src_ready, d, f),
                          path.join(dst_ready, d, f))
                vidx[d] += 1

        if no_video_left:
            break

        while True:
            no_audio_move = True

            for d in afm:
                if aidx[d] < len(afm[d]):
                    ts, f = afm[d][aidx[d]]

                    if ts <= same_vts:
                        no_audio_move = False

                        print(path.join(src_ready, d, f) + ' -> ' +
                              path.join(dst_ready, d, f))

                        copy_move(path.join(src_ready, d, f),
                                  path.join(dst_ready, d, f))
                        aidx[d] += 1

            if no_audio_move:
                break

        time.sleep(args.video_duration)


if __name__ == '__main__':
    main()
