#!/bin/bash -x

curr_dir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
src_path=$1
src_fname=$(basename $src_path)
src_fname_prefix=${src_fname%.*}
dst_dir=$2
bitrate=$3
tmp_file=`mktemp /tmp/XXXXXX.webm`

ffmpeg -nostdin -hide_banner -loglevel panic -y -i $src_path -c:a libopus \
  -af aformat=channel_layouts="7.1|5.1|stereo" -b:a $bitrate \
  -cluster_time_limit 5100 $tmp_file
mv $tmp_file $dst_dir/$src_fname_prefix.webm
