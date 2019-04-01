#!/usr/bin/env python3

from datetime import datetime, timedelta
from subprocess import check_call


def restore_influxdb(fname):
    cmd = './restore_influxdb.py ../databases.yml ' + fname
    print(cmd)
    check_call(cmd, shell=True)


def main():
    start_date = datetime(2019, 1, 1, 11)
    end_date = datetime(2019, 1, 15, 11)
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
