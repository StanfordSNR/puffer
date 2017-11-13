#!/bin/bash -ex

src_fname=`basename $1`
dst_dir=$2
tmp_file=`mktemp`

mv ${tmp_file} ${dst_dir}/"wrong-${src_fname}"
