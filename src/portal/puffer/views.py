import json

from django.shortcuts import render


def index(request):
    return render(request, 'puffer/index.html')


def player(request, aid):
    # parameters passed to Javascript stored in JSON
    params = {'aid': aid, 'session_key': request.session.session_key}
    context = {'params_json': json.dumps(params)}

    return render(request, 'puffer/player.html', context)
