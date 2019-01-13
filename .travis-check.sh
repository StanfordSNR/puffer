#!/bin/bash -ex

cd /home/user/puffer
./fetch_submodules.py
./autogen.sh
./configure
make -j3
make check || (cat src/tests/test-suite.log && exit 1)
