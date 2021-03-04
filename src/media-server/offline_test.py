#!/usr/bin/env python3

import os
from os import path
import time
import signal
import shutil
from subprocess import check_call, Popen


def start_mahimahi_clients(num_clients):
    # create a temporary directory to store Chrome sessions
    chrome_sessions = "chrome-sessions"
    if not path.exists(chrome_sessions):
        os.makedirs(chrome_sessions)

    # declare variable to store a list of processes
    plist = []

    try:
        trace_dir = "/home/ubuntu/fcc_mahimahi_traces"
        # to test nowrway traces use: /home/ubuntu/norway_traces"

        files = os.listdir(trace_dir)
        for filename in files:
            base_port = 9361
            remote_base_port = 10000
            plist = []

            for i in range(1, num_clients + 1):
                port = base_port + i
                remote_debugging_port = remote_base_port + i

                # downlink: trace_dir/TRACE (to transmit video)
                # uplink: 12 Mbps (fast enough for ack messages)
                mahimahi_chrome_cmd = "mm-delay 40 mm-link 12mbps {}/{} -- sh -c 'chromium-browser --headless --disable-gpu --remote-debugging-port={} http://$MAHIMAHI_BASE:8080/player/?wsport={} --user-data-dir={}/{}.profile'".format(trace_dir, filename, remote_debugging_port, port, chrome_sessions, port)
                print(mahimahi_chrome_cmd)

                p = Popen(mahimahi_chrome_cmd.encode('utf-8'),
                          shell=True, preexec_fn=os.setsid)
                plist.append(p)
                time.sleep(4)  # don't know why but seems necessary

            time.sleep(60 * 8)

            for p in plist:
                os.killpg(os.getpgid(p.pid), signal.SIGTERM)
                time.sleep(4)

            shutil.rmtree(chrome_sessions, ignore_errors=True)
    except Exception as e:
        print("exception: " + str(e))
        pass
    finally:
        for p in plist:
            os.killpg(os.getpgid(p.pid), signal.SIGTERM)
            shutil.rmtree(chrome_sessions, ignore_errors=True)


def main():
    # enable IP forwarding
    check_call('sudo sysctl -w net.ipv4.ip_forward=1', shell=True)

    # assume web server and media server are both running
    start_mahimahi_clients(5)


if __name__ == '__main__':
    main()
