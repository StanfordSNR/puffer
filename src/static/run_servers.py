#!/usr/bin/env python3

import os
from os import path
import sys
import argparse
from subprocess import Popen


def link_dist(base_dir):
    src_dist = path.join(base_dir, 'third_party', 'dist-for-puffer')
    dst_dist = path.join(base_dir, 'src', 'static', 'dist')

    if not path.islink(dst_dist) or os.readlink(dst_dist) != src_dist:
        os.symlink(src_dist, dst_dist)
        sys.stderr.write('Created symlink {} -> {}\n'
                         .format(src_dist, dst_dist))


def link_media(base_dir, src_media):
    dst_media = path.join(base_dir, 'src', 'static', 'media')

    if not path.islink(dst_media) or os.readlink(dst_media) != src_media:
        os.symlink(src_media, dst_media)
        sys.stderr.write('Created symlink {} -> {}\n'
                         .format(src_media, dst_media))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('servers_config', help='YAML config file for servers')
    parser.add_argument('--media', required=True,
            help='create a symlink to the directory containing channel media')
    args = parser.parse_args()

    base_dir = path.abspath(path.join(path.dirname(__file__),
                                      os.pardir, os.pardir))

    # create a symbolic link for dist-for-puffer
    link_dist(base_dir)

    # create a symbolic link for media
    link_media(base_dir, path.abspath(args.media))

    procs = []

    # run puffer's ws_media_server
    media_server_src = path.join(base_dir, 'src', 'media-server',
                                 'ws_media_server')
    media_server_cfg = path.abspath(args.servers_config)
    procs.append(Popen([media_server_src, media_server_cfg]))

    # TODO: run the servers of other algorithms

    for proc in procs:
        proc.wait()


if __name__ == '__main__':
    main()
