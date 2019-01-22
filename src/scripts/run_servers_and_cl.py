#!/usr/bin/env python3

import os
import sys
import yaml
import math
import time
import argparse
import signal
from os import path
from datetime import datetime, timedelta
from helpers import Popen, check_call


CL_HOUR = 11  # perform continual learning at 11:00 (UTC)


def run_ttp(ttp_path, yaml_settings_path):
    # load YAML settings from the disk
    with open(yaml_settings_path, 'r') as fh:
        yaml_settings = yaml.safe_load(fh)

    for expt in yaml_settings['experiments']:
        fingerprint = expt['fingerprint']
        abr = fingerprint['abr']
        if abr != 'puffer_ttp':
            continue

        cc = fingerprint['cc']

        # find a name for the new model_dir
        old_model_dir = fingerprint['abr_config']['model_dir']
        model_parent_dir = path.dirname(old_model_dir)

        new_model_base_prefix = cc + '-' + datetime.utcnow().strftime('%Y%m%d')
        # increment i until a non-existent directory is found
        i = 0
        while True:
            i += 1
            new_model_base = new_model_base_prefix + '-' + str(i)
            new_model_dir = path.join(model_parent_dir, new_model_base)
            if not path.isdir(new_model_dir):
                break

        # run ttp.py
        start_time = time.time()
        sys.stderr.write('Continual learning: loaded {} and training {}\n'
                         .format(path.basename(old_model_dir), new_model_base))

        check_call([ttp_path, yaml_settings_path, '--cl', '--cc', cc,
                    '--load-model', old_model_dir,
                    '--save-model', new_model_dir])

        end_time = time.time()
        sys.stderr.write(
            'Continual learning: new model {} is available after {:.2f} hours\n'
            .format(new_model_base, (end_time - start_time) / 3600))

        # update model_dir
        fingerprint['abr_config']['model_dir'] = new_model_dir

    # write YAML settings with updated model_dir back to disk
    with open(yaml_settings_path, 'w') as fh:
        yaml.safe_dump(yaml_settings, fh, default_flow_style=False)
    sys.stderr.write('Updated model_dir in {}\n'.format(yaml_settings_path))


def main():
    parser = argparse.ArgumentParser(
        description='start "run_servers" and continual learning at '
                    '{}:00 (UTC)'.format(CL_HOUR))
    parser.add_argument('yaml_settings')
    args = parser.parse_args()

    yaml_settings_path = path.abspath(args.yaml_settings)
    src_dir = path.dirname(path.dirname(path.abspath(__file__)))
    run_servers_path = path.join(src_dir, 'media-server', 'run_servers')
    ttp_path = path.join(src_dir, 'scripts', 'ttp.py')

    try:
        curr_dt = datetime.utcnow().strftime('%Y-%m-%dT%H:%M:%SZ')
        logfile = open('run_servers_{}.log'.format(curr_dt), 'w')

        # execute run_servers
        run_servers_proc = Popen([run_servers_path, yaml_settings_path],
                                 preexec_fn=os.setsid, stderr=logfile)
        sys.stderr.write('Started run_servers\n')

        while True:
            # sleep until next CL_HOUR
            td = datetime.utcnow()
            wakeup = datetime(td.year, td.month, td.day, CL_HOUR, 0)
            if wakeup <= td:
                wakeup += timedelta(days=1)

            sys.stderr.write('Sleeping until {} (UTC) to perform '
                             'continual learning\n'.format(wakeup))
            time.sleep(math.ceil((wakeup - td).total_seconds()))

            # perform continual learning!
            run_ttp(ttp_path, yaml_settings_path)

            # kill and restart run_servers with updated YAML settings
            if run_servers_proc:
                os.killpg(os.getpgid(run_servers_proc.pid), signal.SIGTERM)

            run_servers_proc = Popen([run_servers_path, yaml_settings_path],
                                     preexec_fn=os.setsid, stderr=logfile)
            sys.stderr.write('Killed and restarted run_servers with updated '
                             'YAML settings\n')
    except Exception as e:
        print(e, file=sys.stderr)
    finally:
        # clean up in case on exceptions
        if run_servers_proc:
            os.killpg(os.getpgid(run_servers_proc.pid), signal.SIGTERM)
        logfile.close()


if __name__ == '__main__':
    main()
