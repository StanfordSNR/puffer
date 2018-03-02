#!/usr/bin/env python3

import argparse
import os
import urllib3
import re


from collections import namedtuple


USERNAME = os.getenv('BLONDER_TONGUE_USERNAME')
PASSWORD = os.getenv('BLONDER_TONGUE_PASSWORD')


SESSION_ID_REGEX = re.compile(r'<input type="hidden" name="session_id" value="(\d+)">')
LOGGED_IN_STR = 'Welcome Admin!  Please wait while retrieving information.'
INPUT_STATUS_REGEX = re.compile(
    r'<tr>\s+'
    r'<td width="2%" bgcolor="#[0-9A-F]+">(?P<input>\d+)</td>\s+'
    r'<td align="center" bgcolor="#[0-9A-F]+">(?P<snr>[\d.]+)</td>\s+'
    r'<td align="center" bgcolor="#[0-9A-F]+">(?P<rf_channel>.+)</td>\s+'
    r'<td align="center" bgcolor="#[0-9A-F]+">(?P<ts_rate>[\d.]+)</td>\s+'
    r'<td align="center" bgcolor="#[0-9A-F]+">(?P<data_rate>[\d.]+)</td>\s+'
    r'</tr>')

InputStatus = namedtuple('InputStatus',
                         ['input', 'snr', 'rf_channel', 'ts_rate', 'data_rate'])


def get_args():
    parser = argparse.ArgumentParser()
    parser.add_argument('host_and_port', type=str,
                        help='Host and port of the http server')
    return parser.parse_args()


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
    r = http.request('POST', 'http://{}/cgi-bin/login.cgi'.format(host_and_port),
                     body=post_params_str, headers=headers)
    if r.status != 200 or LOGGED_IN_STR not in str(r.data):
        raise RuntimeError('Failed to post login credentials')


def get_status_page(http, host_and_port, session_id):
    r = http.request('GET', 'http://{}/cgi-bin/status.cgi?session_id={}'.format(host_and_port, session_id),
                     headers={'Cookie': make_cookie(session_id)})
    if r.status != 200:
        raise RuntimeError('Failed to load information page')
    return str(r.data).replace('\\n', '\n')


def parse_status_html(html):
    matches = INPUT_STATUS_REGEX.findall(html)
    if len(matches) == 0:
        raise RuntimeError('Failed to parse status page')
    return map(lambda x: InputStatus(
        input=int(x[0]),
        snr=float(x[1]),
        rf_channel=x[2],
        ts_rate=float(x[3]),
        data_rate=x[4]), matches)


def main(host_and_port):
    http = urllib3.PoolManager()

    session_id = get_session_id(http, host_and_port)
    post_login(http, host_and_port, session_id)
    status_html = get_status_page(http, host_and_port, session_id)
    statuses = parse_status_html(status_html)

    # TODO: send this information to a database
    for status in statuses:
        print ('Input', status.input)
        print ('\t\tSNR:', status.snr)
        print ('\t\tRF Channel:', status.rf_channel)
        print ('\t\tTS Rate:', status.ts_rate)
        print ('\t\tData Rate:', status.data_rate)


if __name__ == '__main__':
    main(**vars(get_args()))
