#!/bin/bash -ex

curr_dir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
src_path=$1
src_fname=$(basename $src_path)
src_fname_prefix=${src_fname%.*}
dst_dir=$2
tmp_file=`mktemp /tmp/tmp.XXXXXX.webm`
mp4_frag=$curr_dir/../mp4/mp4_fragment

$mp4_frag -m $tmp_file $src_path
mv $tmp_file $dst_dir/$src_fname_prefix.m4s
