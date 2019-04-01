#!/usr/bin/env python3

from datetime import datetime, timedelta
from subprocess import call


def restore_influxdb(fname):
    for cnt in range(1, 4):
        cmd = './restore_influxdb.py ../databases.yml ' + fname
        print('Trial {}:'.format(cnt), cmd)
        if call(cmd, shell=True) == 0:
            break


def main():
    start_date = datetime(2019, 1, 1, 11)
    end_date = datetime(2019, 4, 1, 11)
    f = "%Y-%m-%dT%H"

    s = start_date
    while True:
        e = s + timedelta(days=1)
        if e > end_date:
            break

        fname = s.strftime(f) + '_' + e.strftime(f) + '.tar.gz'
        restore_influxdb(fname)
        s = e


if __name__ == '__main__':
    main()
