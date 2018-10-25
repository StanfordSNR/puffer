from django.contrib.auth import login, authenticate, REDIRECT_FIELD_NAME
from django.contrib.auth.views import LoginView
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
            invite_token = form.cleaned_data.get('invite_token')
            try:
                match_token = InvitationToken.objects.get(token=invite_token)
            except ObjectDoesNotExist:
                messages.error(request, 'Failed to validate the token.')
                return redirect('signup')

            # save the user if the token is validated
            user = form.save()

            # create "addon_cnt" tokens with addon_cnt=0
            if match_token.addon_cnt:
                for _ in range(match_token.addon_cnt):
                    InvitationToken.objects.create(
                            token=random_token(), holder=user)

            # delete used tokens
            match_token.delete()

            messages.success(request,
                'Your account has been created successfully! Please log in.')
            return redirect('login')
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


# Allow admin to generate additional tokens
def generate_token(request):
    if request.user.is_superuser:
        InvitationToken.objects.create(token=random_token(), holder=request.user)

    return render(request, 'puffer/profile.html')
