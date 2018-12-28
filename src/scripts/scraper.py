#!/usr/bin/env python3

import os
import sys
import argparse
import re
import time
import yaml
import subprocess
import requests
from datetime import datetime

from helpers import connect_to_influxdb


USERNAME = os.environ['BLONDER_TONGUE_USERNAME']
PASSWORD = os.environ['BLONDER_TONGUE_PASSWORD']

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

RF_CHANNEL_MAP = {
    7: 'abc',
    12: 'nbc',
    29: 'cbs',
    30: 'pbs',
    34: 'univision',
    41: 'ion',
    44: 'fox',
    45: 'cw'
}


# Send SNR info and more to InfluxDB for monitoring
def send_to_influx(status, yaml_settings):
    curr_time = datetime.utcnow().strftime('%Y-%m-%dT%H:%M:%SZ')

    json_body = []
    for k, v in status.items():
        json_body.append({
          'measurement': 'channel_status',
          'tags': {'channel': v['channel']},
          'time': curr_time,
          'fields': {'snr': v['snr'],
                     'selected_rate': v['selected_rate']}
        })

        sys.stderr.write('channel {}, SNR {}, bitrate {}\n'.format(
            v['channel'], v['snr'], v['selected_rate']))

    client = connect_to_influxdb(yaml_settings)
    client.write_points(json_body, time_precision='s', database='puffer')


def make_cookie(session_id):
    return 'session_id={}'.format(session_id)


def get_session_id(client, login_url):
    r = client.get(login_url)
    if r.status_code != 200:
        raise RuntimeError('Failed to get login page')
    match_object = SESSION_ID_REGEX.search(str(r.text))
    if not match_object:
        raise RuntimeError('Failed to parse session_id from login page')
    return match_object.group(1)


def post_login(client, login_url, session_id):
    post_params = {
        'txtUserName': USERNAME,
        'txtPassword': PASSWORD,
        'session_id': session_id,
        'btnSubmit': 'Submit'
    }

    # Below code handles occasional exceptions which occur during the request
    retry_count = 0
    while True:
        retry_count += 1
        if retry_count >= 2:
            break

        try:
            r = client.post(login_url, data=post_params,
                            headers=dict(Referer=login_url))
            if r.status_code != 200 or LOGGED_IN_STR not in str(r.text):
                raise RuntimeError('Failed to post login credentials')
        except:
            time.sleep(10)


def get_status_page(client, status_url):
    r = client.get(status_url)
    if r.status_code != 200:
        raise RuntimeError('Failed to load information page')
    return str(r.text).replace('\\n', '\n')


def parse_input_status(html, status):
    matches = INPUT_STATUS_REGEX.findall(html)
    if len(matches) == 0:
        raise RuntimeError('Failed to parse input status')

    for x in matches:
        input = int(x[0])
        if input in status:
            status[input]['snr'] = float(x[1])
            status[input]['channel'] = RF_CHANNEL_MAP[int(x[2].split()[0])]


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
    parser.add_argument('yaml_settings')
    parser.add_argument('host_and_port', help='HOST:PORT of server to scrape')
    args = parser.parse_args()

    with open(args.yaml_settings, 'r') as fh:
        yaml_settings = yaml.safe_load(fh)

    host_and_port = args.host_and_port
    login_url = 'http://{}/cgi-bin/login.cgi'.format(host_and_port)

    client = requests.session()

    session_id = get_session_id(client, login_url)
    post_login(client, login_url, session_id)

    status_url = 'http://{}/cgi-bin/status.cgi?session_id={}'.format(
            host_and_port, session_id)
    status_html = get_status_page(client, status_url)

    # interested in all inputs and corresponding outputs
    status = {i:{} for i in range(1, 9)}
    parse_input_status(status_html, status)
    parse_output_status(status_html, status)

    # send snr info to influxdb
    send_to_influx(status, yaml_settings)


if __name__ == '__main__':
    main()
