#!/usr/bin/env python3

import argparse
from subprocess import Popen


channel_configs = {
    #channel_name: (UDP port, TCP port, decoder args),
    'abc': (50001, 60001, '0x31 0x34 720p60 120 900'),
    'nbc': (50002, 60002, '0x31 0x34 1080i30 60 900'),
    'fox': (50003, 60003, '0x31 0x34 720p60 120 900'),
    'pbs': (50004, 60004, '0x31 0x34 1080i30 60 900'),
    'cw': (50005, 60005, '0x31 0x34 1080i30 60 900'),
    'cbs': (50006, 60006, '0x31 0x34 1080i30 60 900'),
    'ion': (50007, 60007, '0x31 0x34 720p60 120 900'),
    'univision': (50008, 60008, '0x41 0x44 720p60 120 900'),
}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('channel', nargs='+')
    args = parser.parse_args()

    procs = []

    video_raw = '/dev/shm/media/{}/working/video-raw'
    audio_raw = '/dev/shm/media/{}/working/audio-raw'
    tmp_raw = '/dev/shm/media/{}/tmp/raw'

    for channel in args.channel:
        config = channel_configs[channel]
        cmd = ('./decoder {} {} {} --tmp {} --tcp 171.64.90.125:{}'.format(
               config[2],
               video_raw.format(channel),
               audio_raw.format(channel),
               tmp_raw.format(channel),
               config[1]))
        print(cmd)
        procs.append(Popen(cmd, shell=True))

    for proc in procs:
        proc.communicate()


if __name__ == '__main__':
    main()
