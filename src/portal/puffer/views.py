import json
import datetime

from django.shortcuts import render, redirect
from django.contrib.auth.decorators import login_required

from .models import Rating


def index(request):
    return render(request, 'puffer/index.html')


@login_required(login_url='/accounts/login/')
def player(request, aid):
    # parameters passed to Javascript stored in JSON
    params = {'aid': aid,
              'session_key': request.session.session_key,
              'username': request.user.username}
    context = {'params_json': json.dumps(params)}

    if 'rating_message' in request.session:
        context['rating_message'] = request.session['rating_message']
        del request.session['rating_message']

    return render(request, 'puffer/player.html', context)


@login_required(login_url='/accounts/login/')
def profile(request):
    return render(request, 'puffer/profile.html')


@login_required(login_url='/accounts/login/')
def rating(request):
    if request.method != 'POST':
        context = {'star_pattern': ['x' * i for i in range(1, 6)]}

        if 'error_message' in request.session:
            context['error_message'] = request.session['error_message']
            del request.session['error_message']

        return render(request, 'puffer/rating.html', context)

    new_star = 0
    new_comment = request.POST['rating-comment']
    if 'rating-star' in request.POST:
      new_star = request.POST['rating-star']

    if new_star == 0 and new_comment == '':
        return redirect('rating')

    try:
        Rating.objects.create(user=request.user, comment_text=new_comment,
                              stars = new_star, pub_date=datetime.datetime.utcnow())
        request.session['rating_message'] = 'Thank you for rating us!'
        return redirect('player', aid=1)
    except:
        request.session['error_message'] = 'Try rating again?'
        return redirect('rating')
