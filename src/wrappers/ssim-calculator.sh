#!/bin/bash -x

curr_dir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
src_path=$1
src_fname=$(basename $src_path)
src_fname_prefix=${src_fname%.*}
dst_dir=$2
tmp_file=$(mktemp /tmp/tmp.XXXXXX.ssim)

# get the path of canonical video
canonical_dir=$3
canonical_video_path=$canonical_dir/$src_fname

ssim_path=$curr_dir/../ssim/ssim
$ssim_path $src_path $canonical_video_path $tmp_file --fast-ssim
mv $tmp_file $dst_dir/$src_fname_prefix.ssim
