import json

from django.shortcuts import render, redirect
from django.utils import timezone

from .models import Comment, StarRating

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
    if request.method != 'POST':
        return render(request, 'puffer/rating.html')

    cur_date=timezone.now()
    flag_comment = 0
    flag_star = 0
    if request.POST['rating-comment'] != '':
        try:
            new_comment = Comment(user=request.user, comment_text=request.POST['rating-comment'],
                                  pub_date=cur_date)
            new_comment.save()
        except:
            return redirect('rating-m', id=0)
        flag_comment = 1

    if 'rating-star' in request.POST:
        try:
            new_star = StarRating(user=request.user, rating=int(request.POST['rating-star']),
                               pub_date=cur_date)
            new_star.save()
        except:
            return redirect('rating-m', id=1)
        flag_star = 1

    if flag_comment == 1 or flag_star == 1:
        return redirect('rating-m', id=flag_comment * 2 + flag_star + 1)
    else:
        return redirect('rating')


def rating_m(request, id):
    if id == 0:
        return render(request, 'puffer/rating.html',
                      {'error_message': 'Something going wrong when submit the comment.'})
    if id == 1:
        return render(request, 'puffer/rating.html',
                      {'error_message': 'Something going wrong when submit the rating.'})
    if id == 2:
        return render(request, 'puffer/rating.html',
                      {'success_message': 'You submited a rating.'})
    if id == 3:
        return render(request, 'puffer/rating.html',
                      {'success_message': 'You submited a comment.'})
    if id == 4:
        return render(request, 'puffer/rating.html',
                      {'success_message': 'You submited a rating and a comment.'})
    return render(request, 'puffer/rating.html',
                      {'error_message': 'Wrong page.'})
