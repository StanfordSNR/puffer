#!/usr/bin/env python3

import os
import sys
import argparse
import re
import time
import subprocess
import urllib3

from datetime import datetime
from influxdb import InfluxDBClient


USERNAME = os.environ['BLONDER_TONGUE_USERNAME']
PASSWORD = os.environ['BLONDER_TONGUE_PASSWORD']
INFLUX_PWD = os.environ['INFLUXDB_PASSWORD']

SESSION_ID_REGEX = re.compile(r'<input type="hidden" name="session_id" value="(\d+)">')
LOGGED_IN_STR = 'Welcome puffer!  Please wait while retrieving information.'
INPUT_STATUS_REGEX = re.compile(
    r'<tr>\s+'
    r'<td width="2%" bgcolor="#[0-9A-F]+">(?P<input>\d+)</td>\s+'
    r'<td align="center" bgcolor="#[0-9A-F]+">(?P<snr>[\d.]+)</td>\s+'
    r'<td align="center" bgcolor="#[0-9A-F]+">(?P<rf_channel>.+)</td>\s+'
    r'<td align="center" bgcolor="#[0-9A-F]+">(?P<ts_rate>[\d.]+)</td>\s+'
    r'<td align="center" bgcolor="#[0-9A-F]+">(?P<data_rate>[\d.]+)</td>\s+'
    r'</tr>')
OUTPUT_STATUS_REGEX = re.compile(
    r'<tr>\s+'
    r'<td bgcolor="#A0A0A0">(?P<input>\d+)</td>\s+'
    r'<td bgcolor="#A0A0A0">.+?</td>\s+'
    r'<td bgcolor="#A0A0A0">.+?</td>\s+'
    r'<td bgcolor="#A0A0A0">.+?</td>\s+'
    r'<td bgcolor="#A0A0A0">(?P<selected_rate>[\d.]+)</td>\s+'
    r'.+?\s+'
    r'</tr>')


# Send SNR info and more to InfluxDB for monitoring
def send_to_influx(status):
    curr_time = datetime.utcnow().strftime('%Y-%m-%dT%H:%M:%SZ')

    json_body = []
    for k, v in status.items():
        json_body.append({
          'measurement': 'channel_status',
          'tags': {'rf_channel': v['rf_channel']},
          'time': curr_time,
          'fields': {'snr': v['snr'],
                     'selected_rate': v['selected_rate']}
        })

        sys.stderr.write('channel {}, SNR {}, bitrate {}\n'.format(
            v['rf_channel'], v['snr'], v['selected_rate']))

    client = InfluxDBClient('localhost', 8086, 'puffer', INFLUX_PWD)
    client.write_points(json_body, time_precision='s', database='collectd')


def make_cookie(session_id):
    return 'session_id={}'.format(session_id)


def get_session_id(http, host_and_port):
    r = http.request('GET', 'http://{}/cgi-bin/login.cgi'.format(host_and_port))
    if r.status != 200:
        raise RuntimeError('Failed to get login page')
    match_object = SESSION_ID_REGEX.search(str(r.data))
    if not match_object:
        raise RuntimeError('Failed to parse session_id from login page')
    return int(match_object.group(1))


def post_login(http, host_and_port, session_id):
    headers = {
        'Cookie': make_cookie(session_id),
        'Content-Type': 'application/x-www-form-urlencoded',
        'Referrer': 'http://{}/cgi-bin/status.cgi?session_id={}'.format(host_and_port, session_id),
    }
    post_params = {
        'txtUserName': USERNAME,
        'txtPassword': PASSWORD,
        'session_id': session_id,
        'btnSubmit': 'Submit'
    }
    post_params_str = '&'.join('{}={}'.format(k, v) for k, v in post_params.items())
    # Below code handles occasional exceptions which occur during the request
    retry_count = 0
    while True:
        try:
            r = http.request('POST', 'http://{}/cgi-bin/login.cgi'.format(host_and_port),
                             body=post_params_str, headers=headers)
            if r.status != 200 or LOGGED_IN_STR not in str(r.data):
                raise RuntimeError('Failed to post login credentials')
        except:
            time.sleep(10)
            retry_count += 1
            if retry_count >= 2:
                continue
        break


def get_status_page(http, host_and_port, session_id):
    r = http.request('GET', 'http://{}/cgi-bin/status.cgi?session_id={}'.format(
                     host_and_port, session_id),
                     headers={'Cookie': make_cookie(session_id)})
    if r.status != 200:
        raise RuntimeError('Failed to load information page')
    return str(r.data).replace('\\n', '\n')


def parse_input_status(html, status):
    matches = INPUT_STATUS_REGEX.findall(html)
    if len(matches) == 0:
        raise RuntimeError('Failed to parse input status')

    for x in matches:
        input = int(x[0])
        if input in status:
            status[input]['snr'] = float(x[1])
            status[input]['rf_channel'] = int(x[2].split()[0])


def parse_output_status(html, status):
    matches = OUTPUT_STATUS_REGEX.findall(html)
    if len(matches) == 0:
        raise RuntimeError('Failed to parse output status')

    for x in matches:
        input = int(x[0])
        if input in status:
            status[input]['selected_rate'] = float(x[1])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('host_and_port', help='HOST:PORT of server to scrape')
    host_and_port = parser.parse_args().host_and_port

    http = urllib3.PoolManager()

    session_id = get_session_id(http, host_and_port)
    post_login(http, host_and_port, session_id)
    status_html = get_status_page(http, host_and_port, session_id)

    # interested in all inputs and corresponding outputs
    status = {i:{} for i in range(1, 9)}
    parse_input_status(status_html, status)
    parse_output_status(status_html, status)

    # send snr info to influxdb
    send_to_influx(status)


if __name__ == '__main__':
    main()
