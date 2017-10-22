#!/bin/bash -ex

src_fname=`basename $1`
dst_dir=$2
tmp_file=`mktemp`

cd ${dst_dir}
mv ${tmp_file} ./"wrong-${src_fname}"
