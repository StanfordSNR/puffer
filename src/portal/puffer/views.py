import os
import json
import random
from datetime import datetime
from influxdb import InfluxDBClient

from django.shortcuts import render, redirect
from django.contrib import messages
from django.contrib.auth.decorators import login_required
from django.core.validators import validate_email
from django.core.exceptions import ValidationError
from django.conf import settings
from django.http import HttpResponse
from accounts.models import InvitationToken
from accounts.utils import random_token

from .models import Rating, GrafanaSnapshot, Participate


def index(request):
    return render(request, 'puffer/index.html')


def faq(request):
    return render(request, 'puffer/faq.html')


def terms(request):
    return render(request, 'puffer/terms.html')


def data(request):
    files = []
    files += [('puffer-fake-sample.tar.gz', '14K', '8b0c8aabb38bd59ed511e938fec129fd8e02ecf015d10c02b254d6804648b9d1')]
    files += [('expt_2020-02-17.json', '225K', '462e8466445f8967ba74cc213aa6c55d4f83b33862e19096abfdb2d754a56cb8')]
    files += [('puffer-201901.tar.gz', '1.9G', 'db9264328d21639dda8693222d72f01132e94c37b243fdb8847012b00c1f13fc')]
    files += [('puffer-201902.tar.gz', '1.5G', 'b0227d6db8a31caba3e1a637b815907cd9fc42b3fce9710d0d77c48ce466f124')]
    files += [('puffer-201903.tar.gz', '1G', '977dd66d46dbfb6677bd7dbdfc8d5b9cefe945342987e5c3ae02b8da50a7015c')]
    files += [('puffer-201904.tar.gz', '1.5G', '95681786f0cc6cf2298963f1e0924a805f1273f6be19a250dd144190261061fe')]
    files += [('puffer-201905.tar.gz', '1.9G', '61f700a0c8d85d3a45974aa25031366ec36aa843b1dbb8291580c7fe73ecce23')]
    files += [('puffer-201906.tar.gz', '2.9G', 'de048f1a1562791a5c894aff6142b82f9135baed0c43ed33aa372e326a673a08')]
    files += [('puffer-201907.tar.gz', '4.5G', '7a32204a159b2f361e5a496659bc134742560c3bf1fd78322c92e150e8779c33')]
    files += [('puffer-201908.tar.gz', '4.1G', 'cc4bb355f592e387262a7febb970bd42c87b80b658d4b11dd015924963d3b235')]
    files += [('puffer-201909.tar.gz', '8.7G', 'a8d11f49cc1367c15d63eb1b08d4ad7dc0fc2c9d5239215f39f319d9a8cfee4e')]
    files += [('puffer-201910.tar.gz', '12G', 'd5b5c8c5c165f2e009a5293dde3b011eab5b1952b562d3a43249925d708a7e0c')]
    files += [('puffer-201911.tar.gz', '12G', '4b6a950a57c6ffcd69607903d00913bd6a2dffe9cfbd5323ba109c5a260fe38a')]
    files += [('puffer-201912.tar.gz', '12G', 'a0fcd9f1010eb43451fcaefc9aaf0ea3a87c25659d330f9131a2bc288596079a')]
    files += [('puffer-202001.tar.gz', '12G', '5453a8f1b53d2426c10a9e13f35bb75091396a29eb69f5a65d8427f3ffffa7b4')]

    return render(request, 'puffer/data.html', {'files': files})


def results(request):
    return render(request, 'puffer/results.html')


@login_required(login_url='/accounts/login/')
def player(request):
    # generate a random port or use a superuser-specified port
    port = None
    if request.user.is_superuser:
        port = request.GET.get('port', None)

    if port is None:
        total_servers = settings.TOTAL_SERVERS
        base_port = settings.WS_BASE_PORT
        port = str(base_port + random.randint(1, total_servers))

    # parameters passed to Javascript stored in JSON
    params = {'session_key': request.session.session_key,
              'username': request.user.username,
              'debug': settings.DEBUG,
              'port': port}
    context = {'params_json': json.dumps(params)}

    return render(request, 'puffer/player.html', context)


@login_required(login_url='/accounts/login/')
def error_reporting(request):
    if request.method == 'POST':
        influx = settings.INFLUXDB
        # ignore reported error if no InfluxDB has been set up
        if influx is None:
            return HttpResponse(status=204)  # No Content

        error_json = json.loads(request.body.decode())

        json_body = [{
            'time': datetime.utcnow().strftime('%Y-%m-%dT%H:%M:%S.%fZ'),
            'measurement': 'client_error',
            'tags': {},
            'fields': {'user': error_json['username'],
                       'init_id': error_json['init_id'],
                       'error': error_json['error']}
        }]

        client = InfluxDBClient(
            influx['host'], influx['port'], influx['user'],
            os.environ[influx['password']], influx['dbname'])
        client.write_points(json_body, time_precision='ms')

        return HttpResponse(status=204)  # No Content
    else:
        return HttpResponse(status=405)  # Method Not Allowed


# functions below are not currently used

@login_required(login_url='/accounts/login/')
def monitoring(request):
    snapshot = GrafanaSnapshot.objects.order_by('-created_on').first()

    if not snapshot:
        return render(request, 'puffer/404.html')

    # only display a snapshot newer than 1 hour ago
    time_diff = datetime.utcnow() - snapshot.created_on
    if time_diff.total_seconds() > 3600:
        return render(request, 'puffer/404.html')

    context = {'snapshot_url': snapshot.url}
    return render(request, 'puffer/monitoring.html', context)


@login_required(login_url='/accounts/login/')
def profile(request):
    if request.method != 'POST':
        return render(request, 'puffer/profile.html')

    if request.user.is_superuser:
        addon_cnt = int(request.POST.get('addon-cnt'))

        InvitationToken.objects.create(
            token=random_token(), holder=request.user, addon_cnt=addon_cnt)

    return render(request, 'puffer/profile.html')


@login_required(login_url='/accounts/login/')
def rating(request):
    if request.method != 'POST':
        context = {'star_pattern': ['x' * i for i in range(1, 6)]}
        return render(request, 'puffer/rating.html', context)

    new_star = 0
    new_comment = request.POST['rating-comment']
    if 'rating-star' in request.POST:
        new_star = request.POST['rating-star']

    if new_star == 0 and new_comment == '':
        messages.info(request, 'Please tell us about our service.')
        return redirect('rating')

    try:
        Rating.objects.create(user=request.user, comment_text=new_comment,
                              stars=new_star, pub_date=datetime.utcnow())
        messages.success(request, 'Thank you for rating us!')
        return redirect('player')
    except:
        messages.error(request, 'Internal error: Please try again.')
        return redirect('rating')


def participate(request):
    if request.method != 'POST':
        return render(request, 'puffer/participate.html')

    email = request.POST['email-field']

    try:
        validate_email(email)
    except ValidationError:
        messages.error(request, 'Please provide a valid email.')
        return redirect('participate')

    try:
        Participate.objects.create(email=email, request_date=datetime.utcnow())
        messages.success(request,
            'Thank you for requesting to participate! We will contact you with'
            ' an invitation code when room becomes available.')
        return redirect('participate')
    except:
        messages.error(request, 'Internal error: Please try again.')
        return redirect('participate')
