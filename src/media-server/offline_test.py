#!/usr/bin/env python3
import time
import os
import subprocess
import signal


def run_offline_media_servers():
    run_servers_cmd = './run_servers ../settings_offline.yml'
    p1 = subprocess.Popen(run_servers_cmd, shell=True)


def start_maimahi_clients(num_clients):
    trace_dir = "/home/ubuntu/fcc_mahimahi_traces"
    files = os.listdir(trace_dir)
    for filename in files:
        mahimahi_cmd = 'mm-delay 40 mm-link 12mbps ' + trace_dir + '/' + \
                        filename
        plist = []
        base_port = 9362
        for i in range(0, num_clients + 1):
            port = base_port + i
            chrome_cmd = 'chromium-browser --headless --disable-gpu --remote-debugging-port=9222 ' + \
                         'http://$MAHIMAHI_BASE:8080/player/?wsport=' + \
                         str(port) + ' --user-data-dir=./' + str(port) + \
                         '.profile'
            chrome_cmd_b = chrome_cmd.encode('utf-8')
            p = subprocess.Popen(mahimahi_cmd, shell=True,
                                 stdin=subprocess.PIPE, preexec_fn=os.setsid)
            p.stdin.write(chrome_cmd_b)
            p.stdin.flush()
            p.stdin.close()
            plist.append(p)

        time.sleep(60*10)
        for p in plist:
            os.killpg(os.getpgid(p.pid), signal.SIGTERM)

        subprocess.check_call("killall chromium-browser", shell=True)
        subprocess.check_call("rm -rf ./*.profile", shell=True,
                              executable='/bin/bash')


def main():
    subprocess.check_call('sudo sysctl -w net.ipv4.ip_forward=1', shell=True)
    # run_media_servers()
    start_maimahi_clients(2)


if __name__ == '__main__':
    main()
