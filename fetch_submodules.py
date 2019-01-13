#!/usr/bin/env python3

import os
from os import path
from subprocess import check_call


def main():
    curr_dir = path.dirname(path.abspath(__file__))
    check_call('git submodule update --recursive --init', shell=True,
               cwd=curr_dir)

    os.chdir(path.join(curr_dir, 'third_party'))
    if path.isdir('libtorch'):
        print('libtorch already exists')
        return

    check_call('wget https://github.com/StanfordSNR/pytorch/releases/download/v1.0.0-puffer/libtorch.tar.gz', shell=True)
    check_call('tar xf libtorch.tar.gz', shell=True)
    check_call('rm -rf libtorch.tar.gz', shell=True)
    print('Fetched libtorch successfully')


if __name__ == '__main__':
    main()
