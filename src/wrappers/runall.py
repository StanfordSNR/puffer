#!/usr/bin/python3

'''
A script to run the entire DASH media pipeline.
'''

import os
from os import path
import sys
import errno
import argparse
import subprocess
from collections import namedtuple
import logging

# current python file location
FILE_DIR = path.dirname(path.realpath(__file__))
# use a mock decoder for now
DECODER_PATH = path.join(FILE_DIR, os.pardir, "mock", "decoder")
CANONICALIZER_PATH = path.join(FILE_DIR, "canonicalizer.sh")
VIDEO_ENCODER_PATH = path.join(FILE_DIR, "video-encoder.sh")
AUDIO_ENCODER_PATH = path.join(FILE_DIR, "audio-encoder.sh")
VIDEO_FRAGMENT_PATH = path.join(FILE_DIR, "video-fragment.sh")
AUDIO_FRAGMENT_PATH = path.join(FILE_DIR, "audio-fragment.sh")

# TODO: build dir can be different from src dir,
# so the paths below can be wrong
MP4_FRAGMENT_PATH = path.join(FILE_DIR, os.pardir, "mp4", "mp4_fragment")
WEBM_FRAGMENT_PATH = path.join(FILE_DIR, os.pardir, "webm", "webm_fragment")
TIME_PATH = path.join(FILE_DIR, os.pardir, "time", "time")
NOTIFIER_PATH = path.join(FILE_DIR, os.pardir, "notifier", "run_notifier")
MONITOR_PATH = path.join(FILE_DIR, os.pardir, "notifier", "monitor")

# filename constants
AUDIO_INIT_NAME = "init.webm"
VIDEO_INIT_NAME = "init.mp4"

# wrapper tuple
VideoConfig = namedtuple("VideoConfig", ["width", "height", "crf"])

# get logger
logger = logging.getLogger("runall")
logger.setLevel(logging.DEBUG)
ch = logging.StreamHandler()
formatter = logging.Formatter("%(asctime)s - %(name)s - %(levelname)s - " +
                              "%(message)s")
ch.setFormatter(formatter)
logger.addHandler(ch)

# pid list to create killall.sh
pid_list = []


def parse_arguments():
    ''' parse command arguments to produce a NameSpace '''
    parser = argparse.ArgumentParser("Run all pipeline components")
    parser.add_argument("-vf", "--video-format", action="append", nargs='+',
                        metavar=("res", "crf"),
                        help="specify the output video format",
                        dest="video_format", required=True)
    parser.add_argument("-af", "--audio-format", action="store", nargs='+',
                        metavar=("bitrate"),
                        help="specify the output audio format",
                        dest="audio_format", required=True)
    parser.add_argument("-p", "--port", action="store", required=True,
                        help="TCP port number", type=int, dest="port")
    parser.add_argument("-o", "--output", action="store", required=True,
                        help="output folder. It will be used as BaseURL.",
                        dest="output")
    parser.add_argument("-m", "--mock-file", action="store",
                        help="mock video file", dest="mock_file")
    return parser.parse_args()


def check_res(res):
    ''' strict check if the given res is a resolution format, i.e., either
        axb or a:b.
    '''
    lst = res.split(":") if ":" in res else res.split("x")
    if len(lst) != 2:
        raise ValueError('resolution must contain a ":" or "x"')

    width, height = lst
    if not width.isdigit() or not height.isdigit():
        raise ValueError('width and height in resolution must be integer')

    return width, height


def make_sure_dir_exists(*args):
    ''' check if given folders exist. if not, this function will create them
    '''
    for dirname in args:
        try:
            os.makedirs(dirname)
        except OSError as exception:
            if exception.errno != errno.EEXIST:
                raise


def run_process(command_str):
    ''' run the command_str as a process with different process group
        this will prevent zombie process
    '''
    # this will ensure the subprocess will continue to run even the bootstrap
    # script exits as well as prevent zombie process
    splitted_commands = command_str.split()
    process = subprocess.Popen(splitted_commands, preexec_fn=os.setpgrp)

    name = path.basename(splitted_commands[0])
    logger.info("Start a new process [" + str(process.pid) + "]: " + name +
                ". commands: " + " ".join(splitted_commands[1:]))
    return process


def run_notifier(notifier_command):
    ''' run the notifier command '''
    return run_process(NOTIFIER_PATH + " " + notifier_command)


def run_monitor(monitor_command):
    ''' run the monitor command '''
    return run_process(MONITOR_PATH + " " + monitor_command)


def get_video_output(output_dir, fmt):
    ''' get video output based on given fmt. name template ###x###-## '''
    return path.join(output_dir, fmt.width + "x" + fmt.height + "-" +
                     fmt.crf)


def get_audio_output(output_dir, bitrate):
    ''' return the actual output for audio segments '''
    return path.join(output_dir, bitrate)


def combine_args(*args):
    ''' combine args together in a pythonic way '''
    return " ".join(args)


def get_media_raw_path(output_folder):
    ''' get audio/video raw folder names '''
    audio_raw_output = path.join(output_folder, "audio_raw")
    video_raw_output = path.join(output_folder, "video_raw")
    make_sure_dir_exists(audio_raw_output, video_raw_output)
    return audio_raw_output, video_raw_output


def get_video_path(output_folder, fmt=None):
    ''' get video folder names, such as canonical, encoded, and final path
        names. if fmt is not given, only video_canonical is returned
    '''
    video_canonical = path.join(output_folder, "video_canonical")
    make_sure_dir_exists(video_canonical)
    if fmt is None:
        return video_canonical

    final_output = get_video_output(output_folder, fmt)
    encoded_output = final_output + "-mp4"
    make_sure_dir_exists(final_output, encoded_output)
    return video_canonical, encoded_output, final_output


def get_audio_path(output_folder, bitrate):
    ''' return the audio path used in audio encoder and frag '''
    # this is also the final output folder basename
    final_output = path.join(output_folder, bitrate)
    encoded_output = final_output + "-webm"
    make_sure_dir_exists(final_output, encoded_output)
    return encoded_output, final_output


def run_decoder(output_folder, port_number, mock_file):
    ''' run the decoder program. currently a mock file is required '''
    # decoder
    audio_raw_output, video_raw_output = get_media_raw_path(output_folder)
    # this is only for mock interface
    decoder_command = combine_args("-p", port_number,
                                   "-a", audio_raw_output,
                                   "-v", video_raw_output, "-m", mock_file)
    pid = run_process(DECODER_PATH + " " + decoder_command)
    pid_list.append(pid)


def run_canonicalizer(output_folder):
    ''' run the canonicalizer '''
    _, video_raw_output = get_media_raw_path(output_folder)
    # video canonicalizer
    video_canonical = get_video_path(output_folder)
    notifier_command = combine_args(video_raw_output, video_canonical,
                                    CANONICALIZER_PATH)
    pid = run_notifier(notifier_command)
    pid_list.append(pid)


def run_video_encoder(video_formats, output_folder):
    ''' run video_encoder.sh for each video format '''
    for fmt in video_formats:
        # this is also the final output folder basename
        res = fmt.width + "x" + fmt.height
        video_canonical, encoded_output, _ = get_video_path(output_folder, fmt)
        notifier_command = combine_args(video_canonical, encoded_output,
                                        VIDEO_ENCODER_PATH, res, fmt.crf)
        # run notifier to encode
        pid = run_notifier(notifier_command)
        pid_list.append(pid)


def run_video_frag(video_formats, output_folder):
    ''' frag video segments as well as generat the init segment '''
    for fmt in video_formats:
        # run frag to output the final segment
        # this is also the final output folder basename
        _, encoded_output, final_output = get_video_path(output_folder, fmt)
        encoded_output = final_output + "-mp4"
        notifier_command = combine_args(encoded_output, final_output,
                                        VIDEO_FRAGMENT_PATH)
        pid = run_notifier(notifier_command)
        pid_list.append(pid)

        # init segment. use monitor's run once command
        video_init_path = path.join(final_output, VIDEO_INIT_NAME)
        monitor_command = combine_args("-q", encoded_output, "-exec",
                                       MP4_FRAGMENT_PATH, "-i",
                                       video_init_path, "{}")
        run_monitor(monitor_command)


def run_audio_encoder(audio_formats, output_folder):
    ''' encode the audio segments into webm format '''
    audio_raw_output, _ = get_media_raw_path(output_folder)
    for bitrate in audio_formats:
        encoded_output, _ = get_audio_path(output_folder, bitrate)
        notifier_command = combine_args(audio_raw_output, encoded_output,
                                        AUDIO_ENCODER_PATH, bitrate)
        # run notifier to encode
        pid = run_notifier(notifier_command)
    pid_list.append(pid)


def run_audio_frag(audio_formats, output_folder):
    ''' frag the audio segment as well as generate the init.webm '''
    for bitrate in audio_formats:
        encoded_output, final_output = get_audio_path(output_folder, bitrate)
        notifier_command = combine_args(encoded_output, final_output,
                                        AUDIO_FRAGMENT_PATH)
        pid = run_notifier(notifier_command)
        pid_list.append(pid)

        # init segment. use monitor's run once command
        audio_init_path = path.join(final_output, AUDIO_INIT_NAME)
        monitor_command = combine_args("-q", encoded_output, "-exec",
                                       WEBM_FRAGMENT_PATH, "-i",
                                       audio_init_path, "{}")
        run_monitor(monitor_command)


def run_time(video_formats, output_folder):
    ''' generate the time content for mpd <UTCTiming> '''
    # run time/time
    time_output = path.join(output_folder, "time")
    time_dirs = " ".join([get_video_output(output_folder, fmt)
                          for fmt in video_formats])
    monitor_command = combine_args("-a", time_dirs, "-exec", TIME_PATH,
                                   "-o", time_output, "{}")
    # run monitor to update the time
    pid = run_monitor(monitor_command)
    pid_list.append(pid)


def main():
    ''' main logic '''
    args = parse_arguments()
    output_folder = args.output
    port_number = str(args.port)
    mock_file = args.mock_file

    # TODO: remove this after the actual decoder is finished
    if mock_file is None:
        sys.exit("currently a mock video file is required")

    video_formats = []
    audio_formats = []

    for res, *crfs in args.video_format:
        width, height = check_res(res)
        for crf in crfs:
            if not crf.isdigit():
                sys.exit("crf must be an integer. got " + crf)
            video_formats.append(VideoConfig(width, height, crf))
    for bitrate in args.audio_format:
        audio_formats.append(bitrate)

    run_canonicalizer(output_folder)
    run_video_encoder(video_formats, output_folder)
    run_video_frag(video_formats, output_folder)
    run_audio_encoder(audio_formats, output_folder)
    run_audio_frag(audio_formats, output_folder)
    run_time(video_formats, output_folder)
    run_decoder(output_folder, port_number, mock_file)

    # create killall list to shutdown the entire pipeline
    if pid_list:
        with open("killall.sh", "w+") as file_sh:
            for pid in pid_list:
                file_sh.write("kill " + str(pid) + "\n")
            file_sh.write("killall monitor")
        os.chmod("killall.sh", 0o744)
        logger.info("killall.sh generated")


if __name__ == "__main__":
    main()
