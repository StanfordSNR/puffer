#!/usr/bin/env python2

# Change to python3 on google cloud, I am using python2 bc i use virtual environments and run_servers
# requires I use my python2 venv for pensieve to work

# This must be called from the media_server directory to work
import time
import os
import subprocess

subprocess.check_call('sudo sysctl -w net.ipv4.ip_forward=1', shell=True);

# Start one media server for each abr
# Comment the below code in to run servers from this file -- works fine, except that
# if you kill the process via ctrl-c the servers keep running sometimes

#run_servers_cmd = './run_servers ../settings_offline.yml'
#p1 = subprocess.Popen(run_servers_cmd, shell=True) # Change to subprocess.DEVNULL

# Now, start mahimahi shells to connect to each
trace_dir = "/home/hudson/cooked_traces/fcc_mahimahi_traces"
files = os.listdir(trace_dir);
for filename in files:
    mahimahi_cmd = 'mm-delay 40 mm-link 12mbps ' + trace_dir + '/' + filename
    plist = []
    base_port = 9362
    for i in range(0, 4):
        port = base_port + i
        chrome_cmd = 'chromium-browser --disable-gpu --remote-debugging-port=9222 ' + \
                     'http://$MAHIMAHI_BASE:8080/player/?wsport=' + str(port) + \
                     ' --user-data-dir=./' + str(port) + '.profile'
        chrome_cmd_b = chrome_cmd.encode('utf-8')
        p = subprocess.Popen(mahimahi_cmd, shell=True, executable='/bin/bash', stdin=subprocess.PIPE);
        p.stdin.write(chrome_cmd_b)
        p.stdin.close()
        plist.append(p)

    time.sleep(60*10)  # 10 minutes, then start over with next trace
    for p in plist:
        p.kill()
    subprocess.check_call("killall chromium-browser", shell=True)
    subprocess.check_call("rm -rf ./*.profile", shell=True, executable='/bin/bash')
