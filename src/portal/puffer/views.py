import json
from datetime import datetime

from django.shortcuts import render, redirect
from django.contrib import messages
from django.contrib.auth.decorators import login_required
from django.core.validators import validate_email
from django.core.exceptions import ValidationError
from django.conf import settings

from .models import Rating, GrafanaSnapshot, Participate


def index(request):
    return render(request, 'puffer/index.html')


@login_required(login_url='/accounts/login/')
def player(request):
    # parameters passed to Javascript stored in JSON
    params = {'session_key': request.session.session_key,
              'username': request.user.username,
              'debug': settings.DEBUG}
    context = {'params_json': json.dumps(params)}

    return render(request, 'puffer/player.html', context)


@login_required(login_url='/accounts/login/')
def profile(request):
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


def faq(request):
    return render(request, 'puffer/faq.html')


def monitoring(request):
    snapshot = GrafanaSnapshot.objects.order_by('-created_on').first()
    if snapshot:
        return redirect(snapshot.url)
    else:
        return redirect('index')
