from django.shortcuts import render, redirect
from django.contrib import messages
from django.urls import reverse
from django.core.exceptions import ObjectDoesNotExist
from django.http import HttpResponse
from accounts.models import InvitationToken
from accounts.forms import SignUpForm
from accounts.utils import random_token


def signup(request):
    # prevent logged in user from signing up
    if request.user.is_authenticated:
        return redirect('index')

    if request.method == 'POST':
        form = SignUpForm(request.POST)

        if form.is_valid():
            user = form.save()

            messages.success(request,
                'Your account has been created successfully! Please log in.')
            return redirect('login')
        else:
            for field_name in form.errors:
                for field_error in form.errors[field_name]:
                    messages.error(request, field_error)
    else:
        form = SignUpForm()

    return render(request, 'accounts/signup.html', {'form': form})


# Allow users to mark tokens as shared or not shared
def share_token(request):
    if request.method == 'POST':
        invite_token = request.POST.get('token')

        try:
            match_token = InvitationToken.objects.get(token=invite_token)
        except ObjectDoesNotExist:
            return HttpResponseBadRequest()

        share = request.POST.get('share')
        if share == 'true':
            match_token.shared = True
        elif share == 'false':
            match_token.shared = False
        else:
            return HttpResponseBadRequest()

        match_token.save()

    return HttpResponse(status=204)
