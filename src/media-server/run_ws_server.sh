#!/bin/bash

config=$1

cd static
python3 -m http.server 8080 &
child_pid=$!
echo HTTP pid $child_pid
cd ..

./ws_media_server $config

kill -9 $child_pid
