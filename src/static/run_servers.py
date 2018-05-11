#!/usr/bin/env python3

import os
from os import path
import sys
import argparse
from subprocess import Popen


def check_before_link(p):
    if path.exists(p) and not path.islink(p):
        sys.exit('Error: {} already exists but is not a symlink'.format(p))


def link_dist(base_dir):
    src_dist = path.join(base_dir, 'third_party', 'dist-for-puffer')
    dst_dist = path.join(base_dir, 'src', 'static', 'dist')
    check_before_link(dst_dist)

    if not path.islink(dst_dist) or os.readlink(dst_dist) != src_dist:
        os.symlink(src_dist, dst_dist)
        sys.stderr.write('Created symlink {} -> {}\n'
                         .format(src_dist, dst_dist))


def link_media(base_dir, src_media):
    dst_media = path.join(base_dir, 'src', 'static', 'media')
    check_before_link(dst_media)

    if src_media is None:
        if not path.islink(dst_media):
            sys.exit('static/media must be a symlink if not specify --media')
    else:
        src_media = path.abspath(src_media)
        if not path.islink(dst_media) or os.readlink(dst_media) != src_media:
            os.symlink(src_media, dst_media)
            sys.stderr.write('Created symlink {} -> {}\n'
                             .format(src_media, dst_media))


def run_servers_in_pensieve(pensieve_dir, procs):
    run_video_src = path.join(pensieve_dir, 'real_exp', 'run_video_servers.py')

    procs.append(Popen([run_video_src, 'RL']))
    procs.append(Popen([run_video_src, 'fastMPC']))
    procs.append(Popen([run_video_src, 'robustMPC']))
    procs.append(Popen([run_video_src, 'other']))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('servers_config', help='YAML config file for servers')
    parser.add_argument('--media',
            help='create a symlink to the directory containing channel media')
    args = parser.parse_args()

    base_dir = path.abspath(path.join(path.dirname(__file__),
                                      os.pardir, os.pardir))

    # check and create a symbolic link for dist-for-puffer
    link_dist(base_dir)

    # check or create a symbolic link for media
    link_media(base_dir, args.media)

    procs = []

    # run puffer's ws_media_server
    media_server_src = path.join(base_dir, 'src', 'media-server',
                                 'ws_media_server')
    media_server_cfg = path.abspath(args.servers_config)
    procs.append(Popen([media_server_src, media_server_cfg]))

    pensieve_dir = path.join(base_dir, 'third_party', 'pensieve')
    run_servers_in_pensieve(pensieve_dir, procs)

    for proc in procs:
        proc.wait()


if __name__ == '__main__':
    main()
