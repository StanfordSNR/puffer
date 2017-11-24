#!/usr/bin/python3

import os
import sys
import argparse
import subprocess
from collections import namedtuple
import logging

# current python file location
FILE_DIR = os.path.dirname(os.path.realpath(__file__))
# use a mock decoder for now
DECODER_PATH = os.path.join(FILE_DIR, "..", "mock", "decoder")
CANONICALIZER_PATH = os.path.join(FILE_DIR, "canonicalizer.sh")
VIDEO_ENCODER_PATH = os.path.join(FILE_DIR, "video-encoder.sh")
AUDIO_ENCODER_PATH = os.path.join(FILE_DIR, "audio-encoder.sh")
VIDEO_FRAGMENT_PATH = os.path.join(FILE_DIR, "video-fragment.sh")
AUDIO_FRAGMENT_PATH = os.path.join(FILE_DIR, "audio-fragment.sh")
TIME_PATH = os.path.join(FILE_DIR, "..", "time", "time")
MPD_WRITER_PATH = os.path.join(FILE_DIR, "..", "mpd", "mpd_writer")
NOTIFIER_PATH = os.path.join(FILE_DIR, "..", "notifier", "run_notifier")
MONITOR_PATH = os.path.join(FILE_DIR, "..", "notifier", "monitor")

# filename constants
AUDIO_INIT_NAME = "init.webm"
VIDEO_INIT_NAME = "init.mp4"

# wrapper tuple
VideoConfig = namedtuple("VideoConfig", ["width", "height", "crf"])

# get logger
logger = logging.getLogger("runall")
logger.setLevel(logging.DEBUG)
ch = logging.StreamHandler()
formatter = logging.Formatter(
        '%(asctime)s - %(name)s - %(levelname)s - %(message)s')
ch.setFormatter(formatter)
logger.addHandler(ch)


def configure_args():
    parser = argparse.ArgumentParser("Run all pipeline components")
    parser.add_argument("-vf", "--video-format", action="append", nargs='+',
                        metavar=("res", "crf"),
                        help="specify the output video format",
                        dest="video_format", required=True)
    parser.add_argument("-af", "--audio-format", action="store", nargs='+',
                        metavar=("bitrate"),
                        help="specify the output audio format",
                        dest="audio_format", required=True, type=int)
    parser.add_argument("-p", "--port", action="store", required=True,
                        help="TCP port number", type=int, dest="port")
    parser.add_argument("-o", "--output", action="store", required=True,
                        help="output folder. It will be used as BaseURL.",
                        dest="output")
    parser.add_argument("-m", "--mock-file", action="store",
                        help="mock video file", dest="mock_file")
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
    for dirname in args:
        if not os.path.isdir(dirname):
            os.makedirs(dirname)


def run_process(command_str):
    # this will ensure the subprocess will continue to run even the bootstrap
    # script exits as well as prevent zombie process
    splitted_commands = command_str.split()
    process = subprocess.Popen(splitted_commands,
                               preexec_fn=os.setpgrp)

    name = os.path.basename(splitted_commands[0])
    logger.info("Start a new process [" + str(process.pid) + "]: " + name +
                ". commands: " + " ".join(splitted_commands[1:]))


def run_notifier(notifier_command):
    run_process(NOTIFIER_PATH + " " + notifier_command)


def run_monitor(monitor_command):
    run_process(MONITOR_PATH + " " + monitor_command)


def get_video_output(output_dir, fmt):
    ''' return the actual output for video segments '''
    return os.path.join(output_dir, fmt.width + "x" + fmt.height + "-" +
                        fmt.crf)


def get_audio_output(output_dir, bitrate):
    ''' return the actual output for audio segments '''
    return os.path.join(output_dir, bitrate)


def combine_args(*args):
    return " ".join(args)


def get_media_raw_path(output_folder):
    audio_raw_output = os.path.join(output_folder, "audio_raw")
    video_raw_output = os.path.join(output_folder, "video_raw")
    check_dir(audio_raw_output, video_raw_output)
    return audio_raw_output, video_raw_output


def get_video_path(output_folder, fmt=None):
    video_canonical = os.path.join(output_folder, "video_canonical")
    check_dir(video_canonical)
    if fmt is None:
        return video_canonical
    final_output = get_video_output(output_folder, fmt)
    encoded_output = final_output + "-mp4"
    # mkdir
    check_dir(final_output, encoded_output)
    return video_canonical, encoded_output, final_output


def get_audio_path(output_folder, bitrate):
    ''' return the audio path used in audio encoder and frag '''
    # this is also the final output folder basename
    final_output = os.path.join(output_folder, bitrate)
    encoded_output = os.path.join(output_folder, bitrate + "-webm")
    # mkdir
    check_dir(final_output, encoded_output)
    return encoded_output, final_output


def run_decoder(output_folder, port_number, mock_file):
    # decoder
    audio_raw_output, video_raw_output = get_media_raw_path(output_folder)
    # this is only for mock interface
    decoder_command = combine_args("-p", str(port_number),
                                   "-a", audio_raw_output,
                                   "-v", video_raw_output, "-m", mock_file)
    run_process(DECODER_PATH + " " + decoder_command)


def run_canonicalizer(output_folder):
    _, video_raw_output = get_media_raw_path(output_folder)
    # video canonicalizer
    video_canonical = get_video_path(output_folder)
    notifier_command = combine_args(video_raw_output, video_canonical,
                                    CANONICALIZER_PATH)
    run_notifier(notifier_command)


def run_video_encoder(video_formats, output_folder):
    for fmt in video_formats:
        # this is also the final output folder basename
        res = fmt.width + "x" + fmt.height
        video_canonical, encoded_output, _ = get_video_path(output_folder, fmt)
        notifier_command = combine_args(video_canonical, encoded_output,
                                        VIDEO_ENCODER_PATH, res, fmt.crf)
        # run notifier to encode
        run_notifier(notifier_command)


def run_video_frag(video_formats, output_folder):
    for fmt in video_formats:
        # run frag to output the final segment
        # this is also the final output folder basename
        _, encoded_output, final_output = get_video_path(output_folder, fmt)
        encoded_output = final_output + "-mp4"
        notifier_command = combine_args(encoded_output, final_output,
                                        VIDEO_FRAGMENT_PATH)
        run_notifier(notifier_command)
        # init segment. use monitor's run once command
        monitor_command = combine_args("-q", encoded_output, "-exec",
                                       VIDEO_FRAGMENT_PATH, "-i",
                                       VIDEO_INIT_NAME, "{}")
        run_monitor(monitor_command)


def run_audio_encoder(audio_formats, output_folder):
    audio_raw_output, _ = get_media_raw_path(output_folder)
    for bitrate in audio_formats:
        encoded_output, _ = get_audio_path(output_folder, bitrate)
        notifier_command = combine_args(audio_raw_output, encoded_output,
                                        AUDIO_ENCODER_PATH, bitrate)
        # run notifier to encode
        run_notifier(notifier_command)


def run_audio_frag(audio_formats, output_folder):
    for bitrate in audio_formats:
        encoded_output, final_output = get_audio_path(output_folder, bitrate)
        notifier_command = combine_args(encoded_output, final_output,
                                        AUDIO_FRAGMENT_PATH)
        run_notifier(notifier_command)
        # init segment. use monitor's run once command
        monitor_command = combine_args("-q", encoded_output, "-exec",
                                       AUDIO_FRAGMENT_PATH, "-i",
                                       AUDIO_INIT_NAME, "{}")
        run_monitor(monitor_command)


def run_time_mpd(output_folder, audio_formats, video_formats):
    # run time/time
    time_output = os.path.join(output_folder, "time")
    time_dirs = " ".join([get_video_output(output_folder, fmt)
                         for fmt in video_formats])
    monitor_command = combine_args("-a", time_dirs, output_folder, "-exec",
                                   TIME_PATH, "-i", "{}", "-o", time_output)
    # run monitor to update the time
    run_monitor(monitor_command)

    # run monitor + mpd_writer for all the output directories
    audio_dir = " ".join([get_audio_output(output_folder, bitrate)
                         for bitrate in audio_formats])
    media_dir = time_dirs[:] + " " + audio_dir
    mpd_path = os.path.join(output_folder, "live.mpd")
    # it will try its best to produce the mpd
    monitor_command = combine_args("-a", "-q", media_dir,
                                   "-exec", MPD_WRITER_PATH,
                                   "-u", output_folder, "-t", "time", "-o",
                                   mpd_path, "--ignore", "{}")
    run_monitor(monitor_command)


def main():
    parser = configure_args()
    arg_output = parser.parse_args()
    output_folder = arg_output.output
    port_number = arg_output.port
    mock_file = arg_output.mock_file

    # TODO: remove this after the actual decoder is finished
    if len(mock_file) == 0:
        sys.exit("currently a mock video file is required")

    video_formats = []
    audio_formats = []

    for res, *crfs in arg_output.video_format:
        width, height = check_res(res)
        for crf in crfs:
            if not crf.isdigit():
                sys.exit("crf must be an integer. got " + crf)
            video_formats.append(VideoConfig(width, height, crf))
    for bitrate in arg_output.audio_format:
        # added as a string because we only need that in Popen
        audio_formats.append(str(bitrate))

    # run_decoder(output_folder, port_number, mock_file)
    # run_canonicalizer(output_folder)
    # run_video_encoder(video_formats, output_folder)
    run_video_frag(video_formats, output_folder)
    # run_audio_encoder(audio_formats, output_folder)
    # run_audio_frag(audio_formats, output_folder)


if __name__ == "__main__":
    main()
