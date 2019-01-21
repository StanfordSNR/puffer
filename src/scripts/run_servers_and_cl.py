#!/usr/bin/env python3

import os
import yaml
import time
import argparse
import signal
from os import path
from datetime import datetime, timedelta
from helpers import Popen


CL_HOUR = 11  # perform continual learning at 11:00 (UTC)
DEVNULL = open(os.devnull, 'w')  # don't actually need to close


def main():
    parser = argparse.ArgumentParser(
        description='start "run_servers" and continual learning at 3AM (UTC)')
    parser.add_argument('yaml_settings')
    args = parser.parse_args()

    yaml_settings_path = args.yaml_settings
    src_dir = path.dirname(path.dirname(path.abspath(__file__)))
    run_servers_path = path.join(src_dir, 'media-server', 'run_servers')

    # execute run_servers
    run_servers_proc = Popen([run_servers_path, yaml_settings_path],
                             preexec_fn=os.setsid, stderr=DEVNULL)

    while True:
        # sleep until next CL_HOUR
        td = datetime.utcnow()
        wakeup = datetime(td.year, td.month, td.day, CL_HOUR, 0)
        if wakeup <= td:
            wakeup += timedelta(days=1)

        print('Will perform continual learning at {} (UTC)'.format(wakeup))
        time.sleep((wakeup - td).total_seconds())

        # TODO: perform continual learning!
        # run_ttp(yaml_settings_path)

        # TODO: update settings.yml
        # update_yaml_settings(yaml_settings_path)

        # kill and restart run_servers
        os.killpg(os.getpgid(run_servers_proc.pid), signal.SIGTERM)
        run_servers_proc = Popen([run_servers_path, yaml_settings_path],
                                 preexec_fn=os.setsid)


if __name__ == '__main__':
    main()
