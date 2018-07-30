import json

from django.shortcuts import render, redirect
import datetime

from .models import Rating

def index(request):
    return render(request, 'puffer/index.html')


def player(request, aid):
    # parameters passed to Javascript stored in JSON
    if request.user.is_authenticated:
        params = {'aid': aid,
                  'session_key': request.session.session_key,
                  'username': request.user.username}
    else:
        params = {'aid': aid}

    context = {'params_json': json.dumps(params)}

    return render(request, 'puffer/player.html', context)


def rating(request):
    new_star = 0
    new_comment = request.POST['rating-comment']

    if 'rating-star' in request.POST:
        try:
            new_star = request.POST['rating-star']
        except:
            return redirect('rating-m', id=1)

    if new_star == 0 and new_comment == '':
        return redirect('rating-m', id=0)

    try:
        Rating.objects.create(user=request.user, comment_text=new_comment,
                              stars = new_star, pub_date=datetime.datetime.utcnow())
        return redirect('rating-m', id=2)
    except:
        return redirect('rating-m', id=1)


def rating_m(request, id):
    context = {'star_pattern': ['x' * i for i in range(1, 6)]}

    if id == 1:
        context['error_message'] = 'Try rating again?'
    if id == 2:
        context['success_message'] = 'Thank you for rating us!'

    return render(request, 'puffer/rating.html', context)
