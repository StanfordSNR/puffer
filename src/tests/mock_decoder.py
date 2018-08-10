#!/usr/bin/env python3

import argparse
from os import path
import subprocess
import inotify.adapters
from shutil import move
from test_helpers import make_sure_path_exists


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('-i', '--input', required=True,
                        help='input media or tcp://localhost:PORT?listen')
    parser.add_argument('-v', '--video', required=True,
                        help='video output folder')
    parser.add_argument('-a', '--audio', required=True,
                        help='audio output folder')
    parser.add_argument('--video-pid', help='PID of video in hex')
    parser.add_argument('--audio-pid', help='PID of audio in hex')
    parser.add_argument('--tmp-dir', help='temporary directory to use')
    args = parser.parse_args()

    input_media = args.input
    video_path = args.video
    make_sure_path_exists(video_path)
    audio_path = args.audio
    make_sure_path_exists(audio_path)
    tmp_dir = args.tmp_dir
    make_sure_path_exists(tmp_dir)

    # construct the string containing PIDs of video and audio
    pid_str = ''
    if args.video_pid is not None and args.audio_pid is not None:
        pid_str = ' -map i:%s -map i:%s' % (args.video_pid, args.audio_pid)

    # run ffmpeg
    ffmpeg_cmd = (
        'ffmpeg -nostdin -hide_banner -loglevel warning -y -i ' + input_media +
        pid_str + ' -an -f segment -segment_time 2.002 ' +
        '-segment_format yuv4mpegpipe ' + path.join(tmp_dir, '%d.y4m') +
        ' -vn -af aresample=async=48000 -f segment -segment_time 4.8 ' +
        '-segment_format wave ' + path.join(tmp_dir, '%d.wav'))
    print(ffmpeg_cmd)
    p = subprocess.Popen(ffmpeg_cmd, shell=True)

    # use inotify to modify the name to have 90k clock
    i = inotify.adapters.Inotify()
    i.add_watch(tmp_dir)

    try:
        for event in i.event_gen():
            if event is not None:
                (header, type_names, watch_path, filename) = event
                if 'IN_CLOSE_WRITE' in type_names:
                    name, ext = path.basename(filename).split('.')
                    ts = int(name)

                    if ext == 'y4m':
                        new_name = str(ts * 180180) + '.y4m'
                        new_path = path.join(video_path, new_name)
                    else:
                        new_name = str(ts * 432000) + '.wav'
                        new_path = path.join(audio_path, new_name)

                    move(path.join(tmp_dir, filename), new_path)
    finally:
        p.kill()
        i.remove_watch(tmp_dir)


if __name__ == '__main__':
    main()
