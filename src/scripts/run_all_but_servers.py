#!/usr/bin/env python3

import os
from os import path
import sys
import time
import argparse
from subprocess import Popen, call, PIPE


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('channel', nargs='+')
    args = parser.parse_args()

    src_dir = path.abspath(path.join(path.dirname(__file__)))

    atsc_dir = path.join(src_dir, 'atsc')
    forwarder_dir = path.join(src_dir, 'forwarder')
    media_server_dir = path.join(src_dir, 'media-server')
    wrappers_dir = path.join(src_dir, 'wrappers')
    monitoring_dir = path.join(src_dir, 'monitoring')

    procs = []

    try:
        # remove *.ts files and run clean_split.py
        call('rm {}/*.ts'.format(atsc_dir), shell=True)
        print('Removed .ts files')

        clean_split = path.join(src_dir, 'tests', 'clean_split.py')
        procs.append(Popen([clean_split, atsc_dir, '10']))
        print('Running clean_split.py')

        # run encoding pipelines
        run_pipeline = path.join(wrappers_dir, 'run_pipeline')
        for channel in args.channel:
            procs.append(Popen([run_pipeline, path.join(wrappers_dir,
                                '{}_pipeline.yml'.format(channel))]))
        print('Running encoding pipelines')

        # run forwarders
        forwarder_procs = []
        for channel in args.channel:
            proc = Popen([path.join(forwarder_dir,
                          '{}_forwarder.sh'.format(channel))], stderr=PIPE)
            procs.append(proc)
            forwarder_procs.append(proc)
        print('Running forwarders')

        # wait until all forwarders successfully start
        for forwarder in forwarder_procs:
            line = forwarder.stderr.readline().decode()
            if 'Listening on TCP' not in line:
                sys.exit('Forwarders not started successfully: ' + line)
            else:
                sys.stderr.write(line)

        # running decoders
        for channel in args.channel:
            procs.append(Popen([path.join(atsc_dir,
                         '{}_decoder.sh'.format(channel))], cwd=atsc_dir))
        print('Running decoders')

        # restart gunicorn
        call('sudo systemctl restart gunicorn', shell=True)
        print('Restarted gunicorn')

        # run reporters: ssim_reporter
        ssim_reporter = path.join(monitoring_dir, 'ssim_reporter')
        procs.append(Popen([ssim_reporter, '/dev/shm/media']))
        print('Running ssim_reporter')

        for proc in procs:
            proc.communicate()
    except Exception as e:
        print(e)
    finally:
        for proc in procs:
            proc.kill()

        # kill forwarders running on the tv-nuc
        call('ssh ubuntu@tv-nuc.stanford.edu "pkill -f '
             '/home/ubuntu/puffer/src/forwarder/forwarder"', shell=True)
        print('pkilled forwarders on tv-nuc.stanford.edu')


if __name__ == '__main__':
    main()
