#!/bin/bash -ex

cd /home/user/puffer
./fetch-submodules.sh
./autogen.sh
./configure
make -j3
make check || (cat src/tests/test-suite.log && exit 1)
