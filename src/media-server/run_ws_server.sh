#!/bin/bash

config=$1

cd static
python3 -m http.server 8080 &
cd ..

./ws_media_server $config
