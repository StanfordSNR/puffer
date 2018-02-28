#!/bin/bash -ex

src_fname=`basename $1`
dst_file=$2
tmp_file=`mktemp`

mv ${tmp_file} ${dst_file}
