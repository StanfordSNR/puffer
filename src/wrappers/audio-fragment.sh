#!/bin/bash -ex

curr_dir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
src_path=$1
src_fname=$(basename $src_path)
src_fname_prefix=${src_fname%.*}
dst_dir=$2
tmp_file=`mktemp /tmp/tmp.XXXXXX.chk`
webm_frag=$curr_dir/../webm/webm_fragment

# optional argument for timecode file
if [ -z "$3" ]
then
  $webm_frag -m $tmp_file $src_path
else
  $webm_frag -m $tmp_file -t $3 $src_path
fi

mv $tmp_file $dst_dir/$src_fname_prefix.chk
