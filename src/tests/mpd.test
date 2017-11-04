#!/bin/bash -ex

pwd="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

if [ -z $test_tmpdir ];then
    test_tmpdir="test_tmp"
    mkdir -p $test_tmpdir
fi

if [ -z $abs_builddir ]; then
    $abs_builddir=$pwd
fi

# fetch the files
TEST_VECTOR="$test_tmpdir/test-vectors-mpd"
# fetch the files
rm -rf $TEST_VECTOR
git clone https://github.com/StanfordSNR/tv-test-vectors $TEST_VECTOR

MPD_WRITER=$abs_builddir/../mpd/mpd_writer
MPD_DIR="$TEST_VECTOR/test_mpd"
REFERENCE="$MPD_DIR/reference.mpd"
MP4_DIR="$MPD_DIR/video $MPD_DIR/audio"

MPD_OUTPUT="$test_tmpdir/output.mpd"

MPD_ARG="-a 0 -v 1 -p 1400000000 -o $MPD_OUTPUT"

# run the command
$MPD_WRITER $MP4_DIR $MPD_ARG

# compare the result
diff $MPD_OUTPUT $REFERENCE
exit $?
