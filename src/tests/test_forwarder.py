#!/usr/bin/env python

import os
from os import path
import random
import string
from time import sleep
from test_helpers import get_open_port, Popen, check_call, check_output


def main():
    # generate a random file
    content = ''.join(random.choice(string.letters) for _ in xrange(2000))

    abs_builddir = os.environ['abs_builddir']
    test_tmpdir = path.join(abs_builddir, 'test_tmpdir')

    orig_file = path.join(test_tmpdir, 'orig_file')
    copy_file = path.join(test_tmpdir, 'copy_file')

    with open(orig_file, 'w') as f:
        f.write(content)

    # run TCP receiver
    tcp_port = get_open_port()
    tcp_proc = Popen('nc -l %s > %s' % (tcp_port, copy_file), shell=True)
    sleep(0.5)

    # run forwarder
    forwarder = path.abspath(
        path.join(abs_builddir, os.pardir, 'forwarder', 'forwarder'))

    udp_port = get_open_port()
    forwarder_proc = Popen([forwarder, '--udp-port', udp_port,
                            '--tcp', '127.0.0.1:' + tcp_port])
    sleep(0.5)

    # run UDP sender, which will after completing the file transfer
    check_call('nc -w 0 -u 127.0.0.1 %s < %s' %
               (udp_port, orig_file), shell=True)
    sleep(0.5)

    forwarder_proc.terminate()
    tcp_proc.terminate()

    # compare orig_file and copy_file
    if not path.exists(copy_file):
        sys.exit('%s does not exist' % copy_file)

    if check_output(['diff', orig_file, copy_file]):
        sys.exit('%s and %s diff' % (orig_file, copy_file))


if __name__ == '__main__':
    main()
