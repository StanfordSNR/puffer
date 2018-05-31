from django.shortcuts import render


def index(request):
    return render(request, 'puffer/index.html')


def player(request, aid):
    context = {'aid': aid}
    return render(request, 'puffer/player.html', context)
