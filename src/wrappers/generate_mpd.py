#!/usr/bin/python3
'''
A python script to generate a dynamic mpd using the structure from 'runall.py'
'''
import os
from os import path
import sys
import argparse
import glob

# use some functions from runall
from runall import combine_args, run_process

# current python file location
FILE_DIR = path.dirname(path.realpath(__file__))
MPD_WRITER_PATH = path.join(FILE_DIR, os.pardir, "mpd", "mpd_writer")


def parse_arguments():
    ''' parse command arguments to produce a NameSpace '''
    parser = argparse.ArgumentParser("Genete mpd from 'runall.py'")
    parser.add_argument("-i", "--input", help="the output dir specified in" +
                        " 'runall.py'", action="store", required=True,
                        dest="input_dir")
    parser.add_argument("-o", "--name", help="filename of the mpd, such as " +
                        "live.mpd", action="store", default="live.mpd",
                        dest="mpd_name")
    return parser.parse_args()


def is_audio(dirname):
    ''' check if <dirname> is an audio media directory '''
    name = path.basename(dirname)   # remove the output_folder name
    if not path.isdir(dirname):
        return False
    if not name.isdigit():
        return False
    if not path.exists(path.join(dirname, "init.webm")):
        return False
    if not glob.glob(path.join(dirname, "*.chk")):
        return False
    return True


def is_video(dirname):
    ''' check if <dirname> is a video media directory '''
    if not path.isdir(dirname):
        return False
    if not path.exists(path.join(dirname, "init.mp4")):
        return False
    if not glob.glob(path.join(dirname, "*.m4s")):
        return False
    return True


def main():
    ''' main logic '''
    args = parse_arguments()
    input_dir = args.input_dir

    # assume some parameters
    base_url = input_dir
    time_url = path.join(input_dir, "time")
    mpd_path = path.join(input_dir, args.mpd_name)

    # get top level subdirectory
    sub_dirs = [path.join(input_dir, sub) for sub in os.listdir(input_dir)
                if path.isdir(path.join(input_dir, sub))]
    # media list to pass to mpd_writer
    media_list = []
    for dir_name in sub_dirs:
        if is_audio(dir_name) or is_video(dir_name):
            media_list.append(dir_name)

    if not media_list:
        sys.exit("No media folder found in " + input_dir)

    mpd_command = combine_args(MPD_WRITER_PATH, "-t", time_url, "-u", base_url,
                               "-o", mpd_path, " ".join(media_list))
    process = run_process(mpd_command)
    process.wait()  # wait till it's finished

if __name__ == "__main__":
    main()
