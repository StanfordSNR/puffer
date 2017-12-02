#!/bin/bash -x

curr_dir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
src_path=$1
src_fname=$(basename $src_path)
src_fname_prefix=${src_fname%.*}
dst_dir=$2
tmp_file=$(mktemp /tmp/tmp.XXXXXX.mp4)

ffmpeg -nostdin -hide_banner -loglevel panic -y -i $src_path -vf yadif \
  -c:v libx264 -crf 18 -preset ultrafast $tmp_file
mv $tmp_file $dst_dir/$src_fname_prefix.mp4
rm -f $src_path
