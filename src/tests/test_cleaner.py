#!/usr/bin/env python

import os
from os import path
import sys
import time
from test_helpers import check_call

def main():
    abs_srcdir = os.environ['abs_srcdir']

    abs_builddir = os.environ['abs_builddir']
    test_tmpdir = path.join(abs_builddir, 'test_tmpdir')

    cleaner_testdir = path.join(test_tmpdir, 'cleaner_testdir')

    check_call(['mkdir', '-p', cleaner_testdir])

    test_file1 = path.join(cleaner_testdir, "t1")
    test_file2 = path.join(cleaner_testdir, "t2")

    cleaner = path.abspath(
            path.join(abs_builddir, os.pardir, 'cleaner', 'cleaner'))

    cmd = [cleaner, cleaner_testdir, "-p", 't\d+', "-t", "1"]

    start_time = time.time()
    check_call(["touch", test_file1])

    time.sleep(1)

    check_call(["touch", test_file2])

    # run the cleaner
    check_call(cmd)
    # this should only delete t1
    if os.path.isfile(test_file1):
        exit(1)
    if not os.path.isfile(test_file2):
        exit(1)


if __name__ == "__main__":
    main()
