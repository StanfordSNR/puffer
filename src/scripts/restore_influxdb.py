#!/usr/bin/env python3

import argparse
from helpers import check_call


def download_untar(file_to_restore):
    # download
    cmd = 'gsutil cp gs://puffer-influxdb-backup/{} .'.format(file_to_restore)
    check_call(cmd, shell=True)

    # untar
    cmd = 'tar xf {}'.format(file_to_restore)
    check_call(cmd, shell=True)

    # remove tar
    cmd = 'rm -f {}'.format(file_to_restore)
    check_call(cmd, shell=True)

    return file_to_restore[:file_to_restore.index('.')]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('file_to_restore',
                        help='e.g., 2018-12-04T11_2018-12-05T11.tar.gz')
    args = parser.parse_args()
    file_to_restore = args.file_to_restore

    # download data from Google cloud
    filename = download_untar(file_to_restore)


if __name__ == '__main__':
    main()
