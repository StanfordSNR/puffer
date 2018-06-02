import json

from django.shortcuts import render


def index(request):
    return render(request, 'puffer/index.html')


def player(request, aid):
    # parameters passed to Javascript stored in JSON
    token_set = None
    if request.user.is_authenticated:
        token_set = request.user.invitationtoken_set.all()
    params = {'aid': aid, 'session_key': request.session.session_key}
    context = {'params_json': json.dumps(params)}

    return render(request, 'puffer/player.html', context)
