#!/usr/bin/python3

import os
import sys
import argparse
import subprocess
from collections import namedtuple
import logging
import time

# current python file location
FILE_DIR = os.path.dirname(os.path.realpath(__file__))
# use a mock decoder for now
DECODER_PATH = os.path.join(FILE_DIR, "..", "mock", "decoder")
VIDEO_ENCODER_PATH = os.path.join(FILE_DIR, "video-encoder.sh")
AUDIO_ENCODER_PATH = os.path.join(FILE_DIR, "audio-encoder.sh")
VIDEO_FRAGMENT_PATH = os.path.join(FILE_DIR, "video-fragment.sh")
AUDIO_FRAGMENT_PATH = os.path.join(FILE_DIR, "audio-fragment.sh")
TIME_PATH = os.path.join(FILE_DIR, "..", "time", "time")
MPD_PATH = os.path.join(FILE_DIR, "..", "mpd", "mpd_writer")
NOTIFIER_PATH = os.path.join(FILE_DIR, "..", "notifier", "run_notifier")
# TODO: add cleaner

# wrapper tuple
VideoConfig = namedtuple("VideoConfig", ["width", "height", "crf"])

# get logger
logger = logging.getLogger("runall")
logger.setLevel(logging.DEBUG)
ch = logging.StreamHandler()
formatter = logging.Formatter('%(asctime)s - %(name)s - %(levelname)s - %(message)s')
ch.setFormatter(formatter)
logger.addHandler(ch)

def configure_args():
    parser= argparse.ArgumentParser("Run all pipeline components")
    parser.add_argument("-vf", "--video-format", action = "append", nargs = '+',
            metavar = ("res","crf"), help = "specify the output video format",
            dest = "video_format", required = True)
    parser.add_argument("-af", "--audio-format", action = "store", nargs = '+',
            metavar = ("bitrate"), help = "specify the output audio format",
            dest = "audio_format", required = True, type=int)
    parser.add_argument("-p", "--port", action = "store", required = True,
            help = "TCP port number", type = int, dest = "port")
    parser.add_argument("-o", "--output", action = "store", required = True,
            help = "output folder. It will be used as BaseURL as well.",
            dest = "output")
    return parser

def check_res(res):
    lst = res.split(":") if ":" in res else res.split("x")
    if len(lst) != 2:
        return None
    width, height = lst
    if not width.isdigit() or not height.isdigit():
        return None
    return width, height

def check_dir(*args):
    for d in args:
        if not os.path.isdir(d):
            os.makedirs(d)

def run_process(command_str):
    # this will ensure the subprocess will continue to run even the bootstrap
    # script exits as well as prevent zombie process
    splitted_commands = command_str.split()
    p = subprocess.Popen(splitted_commands,
                     stdout=open('/dev/null', 'w'),
                     preexec_fn=os.setpgrp)

    name = os.path.basename(splitted_commands[0])
    logger.info("Start a new process [" + str(p.pid) + "]: " + name)

def main():
    parser = configure_args()
    arg_output = parser.parse_args()
    output_folder = arg_output.output
    port_number = arg_output.port

    video_formats = []
    audio_formats = []
    output_list = []

    for res, *crfs in arg_output.video_format:
        width, height = check_res(res)
        for crf in crfs:
            if not crf.isdigit():
                print("crf must be an integer")
                exit(1)
            video_formats.append(VideoConfig(width, height, crf))
    for bitrate in arg_output.audio_format:
        # added as a string because we only need that in Popen
        audio_formats.append(str(bitrate))

    # decoder
    audio_raw_output = os.path.join(output_folder, "audio_raw")
    video_raw_output = os.path.join(output_folder, "video_raw")
    check_dir(audio_raw_output, video_raw_output)
    # this is only for mock interface
    decoder_command = "-p " + str(port_number) + " -a " + audio_raw_output +\
            " -v " + video_raw_output
    logger.info("Decoder path: " + DECODER_PATH)
    logger.info("Decoder command: " +  decoder_command)
    run_process(DECODER_PATH + " " + decoder_command)

    # (notifier + video_encoder) + (notifier + video_frag)
    for fmt in video_formats:
        # this is also the final output folder basename
        res = fmt.width + "x" + fmt.height
        crf = fmt.crf
        final_output = os.path.join(output_folder, res + "-" + crf)
        encoded_output = os.path.join(output_folder, res + "-" + crf + "-mp4")
        output_list.append(final_output)
        # mkdir
        check_dir(final_output, encoded_output)
        notifier_command = video_raw_output + " " + encoded_output + " " + \
                VIDEO_ENCODER_PATH + " " + res  + " " + fmt.crf
        logger.info("Using notifier command " + notifier_command)
        # run notifier to encode
        run_process(NOTIFIER_PATH + " " + notifier_command)

        # run frag to output the final segment
        # TODO: this is a bug for initial segment
        notifier_command = encoded_output + " " + final_output + " " + \
                VIDEO_FRAGMENT_PATH
        logger.info("Using notifier command " + notifier_command)
        run_process(NOTIFIER_PATH + " " + notifier_command)

    # (notifier + video_encoder) + (notifier + video_frag)
    for bitrate in audio_formats:
        # this is also the final output folder basename
        final_output = os.path.join(output_folder, bitrate)
        encoded_output = os.path.join(output_folder, bitrate + "-webm")
        output_list.append(final_output)
        # mkdir
        check_dir(final_output, encoded_output)
        notifier_command = audio_raw_output + " " + encoded_output + " " + \
                AUDIO_ENCODER_PATH + " " + bitrate
        logger.info("Using notifier command " + notifier_command)
        # run notifier to encode
        run_process(NOTIFIER_PATH + " " + notifier_command)

        # run frag to output the final segment
        # TODO: this is a bug for initial segment
        notifier_command = encoded_output + " " + final_output + " " + \
                AUDIO_FRAGMENT_PATH
        logger.info("Using notifier command " + notifier_command)
        run_process(NOTIFIER_PATH + " " + notifier_command)


if __name__ == "__main__":
    main()
