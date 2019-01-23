import os
import json
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


# turn off user authentication
# @login_required(login_url='/accounts/login/')
def player(request):
    # parameters passed to Javascript stored in JSON
    params = {'session_key': 'fake',
              'username': 'fake',
              'debug': settings.DEBUG}
    context = {'params_json': json.dumps(params)}

    return render(request, 'puffer/player.html', context)


@login_required(login_url='/accounts/login/')
def error_reporting(request):
    if request.method == 'POST':
        influx = settings.INFLUXDB
        # ignore reported error if no InfluxDB has been set up
        if influx is None:
            return HttpResponse(status=204)  # No Content

        ts = datetime.utcnow().strftime('%Y-%m-%dT%H:%M:%S.%fZ')
        error_json = json.loads(request.body.decode())

        json_body = [{
            'measurement': 'client_error',
            'tags': {'user': error_json['username']},
            'time': ts,
            'fields': {'init_id': error_json['init_id'],
                       'error': error_json['error']}
        }]

        client = InfluxDBClient(
            influx['host'], influx['port'], influx['user'],
            os.environ[influx['password']], influx['dbname'])
        client.write_points(json_body, time_precision='ms')

        return HttpResponse(status=204)  # No Content
    else:
        return HttpResponse(status=405)  # Method Not Allowed


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


# functions below are not currently used

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
