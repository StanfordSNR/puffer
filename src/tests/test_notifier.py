#!/usr/bin/env python

import os
from os import path
import sys
from test_helpers import check_call, check_output, Popen, PIPE


def create_tmp_and_move(dir_move_to):
    tmp_file = check_output(['mktemp']).strip()
    new_file = path.join(dir_move_to, path.basename(tmp_file) + '.file')
    check_call(['mv', tmp_file, new_file])


def expect_output(proc, program_name):
    while True:
        line = proc.stderr.readline()

        if '%s failed' % program_name in line:
            return False
        elif '%s succeeded' % program_name in line:
            return True


def main():
    abs_srcdir = os.environ['abs_srcdir']

    abs_builddir = os.environ['abs_builddir']
    test_tmpdir = path.join(abs_builddir, 'test_tmpdir')

    notifier_srcdir = path.join(test_tmpdir, 'notifier_srcdir')
    notifier_dstdir = path.join(test_tmpdir, 'notifier_dstdir')

    check_call(['mkdir', '-p', notifier_srcdir])
    check_call(['mkdir', '-p', notifier_dstdir])

    # create pre-existing files ahead of running notifier
    existing_test_file = path.join(notifier_srcdir, 'existing-test.file')
    check_call(['touch', existing_test_file])

    run_notifier = path.abspath(
            path.join(abs_builddir, os.pardir, 'notifier', 'run_notifier'))

    # run a notifier that will call the "good" program
    notified_good_prog_name = 'notified_good_prog.sh'
    notified_good_prog = path.join(abs_srcdir, notified_good_prog_name)

    cmd = [run_notifier, notifier_srcdir, notifier_dstdir, notified_good_prog]
    good_proc = Popen(cmd, stderr=PIPE)

    # expect pre-existing files to be processed
    if not expect_output(good_proc, notified_good_prog_name):
        sys.exit('test failed: ' + ' '.join(cmd))

    # create a tmp file and expect it to be processed correctly
    create_tmp_and_move(notifier_srcdir)
    if not expect_output(good_proc, notified_good_prog_name):
        sys.exit('test failed: ' + ' '.join(cmd))

    good_proc.terminate()

    # run a notifier that will call the "bad" program
    notified_bad_prog_name = 'notified_bad_prog.sh'
    notified_bad_prog = path.join(abs_srcdir, notified_bad_prog_name)

    cmd = [run_notifier, notifier_srcdir, notifier_dstdir, notified_bad_prog]
    bad_proc = Popen(cmd, stderr=PIPE)

    # create a tmp file and expect it NOT to be processed correctly
    create_tmp_and_move(notifier_srcdir)
    if expect_output(bad_proc, notified_bad_prog_name):
        sys.exit('test failed: ' + ' '.join(cmd))

    bad_proc.terminate()


if __name__ == '__main__':
    main()
