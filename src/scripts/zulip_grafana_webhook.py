#!/usr/bin/env python3

import os
import sys
import requests
from flask import Flask, request, abort
from urllib.parse import urlparse


app = Flask(__name__)

ZULIP_URL = os.environ['ZULIP_URL']
ZULIP_BOT_EMAIL = os.environ['ZULIP_BOT_EMAIL']
ZULIP_BOT_TOKEN = os.environ['ZULIP_BOT_TOKEN']

PUFFER_LOCNET = 'puffer.stanford.edu'


@app.route('/', methods=['POST'])
def webhook():
    if request.method != 'POST':
        abort(400)

    hook_data = request.json

    title = '**' + hook_data['title'] + '**'
    title_icon = ''
    if hook_data['state'] == 'ok':
        title_icon = ' :check_mark:'
    else:
        title_icon = ' :warning:'

    metrics = ''.join(['* ' + x['metric'] + ': ' + str(x['value']) + '\n'
                      for x in hook_data['evalMatches']])
    if 'message' in hook_data:
        message = hook_data['message'] + '\n'
    else:
        message = ''

    details_url = urlparse(hook_data['ruleUrl'])._replace(netloc=PUFFER_LOCNET)
    details = '[details](' + details_url.geturl() + ')'

    data = [
        ('type', 'stream'),
        ('to', 'puffer-alert'),
        ('subject', 'Alert'),
        ('content', title + title_icon + '\n' + message + metrics + '\n' + details)
    ]

    response = requests.post(ZULIP_URL, data=data,
                             auth=(ZULIP_BOT_EMAIL, ZULIP_BOT_TOKEN))

    if response.status_code == requests.codes.ok:
        print('Posted an alert successfully')
    else:
        print('Failed to post the alert')

    return '', 200


if __name__ == '__main__':
    app.run()
