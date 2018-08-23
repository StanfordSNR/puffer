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
    print('webhook')
    sys.stdout.flush()
    if request.method != 'POST':
        abort(400)

    hook_data = request.json

    title = '**' + hook_data['title'] + '**'
    title_icon = ''
    if hook_data['state'] == 'ok':
        title_icon = ' :check_mark:'
    if hook_data['state'] == 'alerting':
        title_icon = ' :warning:'

    metrics = ''.join(['* ' + x['metric'] + ': ' + str(x['value']) + '\n'
                      for x in hook_data['evalMatches']])
    if 'message' in hook_data:
        message = hook_data['message'] + '\n'
    else:
        message = ''

    details_url = urlparse(hook_data['ruleUrl'])._replace(netloc=PUFFER_LOCNET)
    details = '[details](' + details_url.geturl() + ')'

    print('Hook data:', hook_data)

    data = [
        ('type', 'stream'),
        ('to', 'puffer-alert'),
        ('subject', 'Test'),
        ('content', title + title_icon + '\n' + message + metrics + '\n' + details)
    ]

    response = requests.post(ZULIP_URL, data=data,
                             auth=(ZULIP_BOT_EMAIL, ZULIP_BOT_TOKEN))
    print(response)
    return '', 200


if __name__ == '__main__':
    app.run()
