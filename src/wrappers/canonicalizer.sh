#!/bin/bash -ex

curr_dir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
src_path=$1
src_fname=$(basename $src_path)
src_fname_prefix=${src_fname%.*}
dst_dir=$2
tmp_file=$(mktemp /tmp/tmp.XXXXXX.mp4)

ffmpeg -y -i $src_path -vf yadif=1 $tmp_file
mv $tmp_file $dst_dir/$src_fname_prefix.mp4
