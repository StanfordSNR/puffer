#!/bin/bash -ex

curr_dir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
src_path=$1
src_fname=$(basename $src_path)
src_fname_prefix=${src_fname%.*}
dst_dir=$2
bitrate=$3
tmp_file=`mktemp /tmp/tmp.XXXXXX.webm`

ffmpeg -y -i $src_path -c:a libopus -b:a $bitrate -cluster_time_limit 5100 $tmp_file
mv $tmp_file $dst_dir/$res/$src_fname_prefix.webm
