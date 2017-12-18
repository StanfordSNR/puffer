#!/bin/bash -x

curr_dir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
src_path=$1
src_fname=$(basename $src_path)
src_fname_prefix=${src_fname%.*}
dst_dir=$2

tmp_folder=/dev/shm/canonicalizer-tmp
mkdir -p $tmp_folder
tmp_file=$(mktemp $tmp_folder/XXXXXX.y4m)

ffmpeg -nostdin -hide_banner -loglevel panic -y -i $src_path -vf yadif \
  $tmp_file
mv $tmp_file $dst_dir/$src_fname_prefix.y4m
rm -f $src_path
