#!/bin/bash

if [ "$#" -ne 1 ]; then
  echo "Usage: ./run_ws_server.sh <yaml_config>"
  exit
fi

config=$1

./ws_media_server $config &
child_pid=$!

cd static
echo HTTP server pid $child_pid
python3 -m http.server 8080
cd ..

kill -9 $child_pid
