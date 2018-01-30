#!/bin/bash -x

curr_dir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
src_path=$1
src_fname=$(basename $src_path)
src_fname_prefix=${src_fname%.*}
dst_dir=$2

# get the path of canonical video
canonical_dir=$3
canonical_y4m=$canonical_dir/$src_fname_prefix.y4m

# get temporary folder path
tmp_folder=$4

# get resolution of the canonical video
width_prefix="streams_stream_0_width="
height_prefix="streams_stream_0_height="
declare -a resolution
while read -r line
do
  resolution+=($line)
done < <(ffprobe -v error -of flat=s=_ -select_streams v:0 -show_entries \
         stream=width,height $canonical_y4m)
width_with_prefix=${resolution[0]}
height_with_prefix=${resolution[1]}
width=${width_with_prefix#${width_prefix}}
height=${height_with_prefix#${height_prefix}}

# convert src_path to Y4M with the same resolution
mkdir -p $tmp_folder
src_y4m=$(mktemp $tmp_folder/XXXXXX.y4m)
ffmpeg -nostdin -hide_banner -loglevel panic -y -i $src_path \
  -f yuv4mpegpipe -vf scale=$width:$height $src_y4m

# calculate SSIM
tmp_file=$(mktemp /tmp/XXXXXX.ssim)
ssim_path=$curr_dir/../ssim/ssim
$ssim_path $src_y4m $canonical_y4m $tmp_file -n 10
mv $tmp_file $dst_dir/$src_fname_prefix.ssim
rm -f $src_y4m
