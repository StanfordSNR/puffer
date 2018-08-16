from django.contrib.auth import login, authenticate, REDIRECT_FIELD_NAME
from django.contrib.auth import views as auth_views
from django.shortcuts import render, redirect
from django.contrib import messages
from django.urls import reverse
from django.core.exceptions import ObjectDoesNotExist
from accounts.models import InvitationToken
from accounts.forms import SignUpForm
from accounts.utils import random_token


def my_login(request):
    # prevent logged in user from logging in
    if request.user.is_authenticated:
        return redirect('index')

    if request.method == 'POST':
        if 'america' not in request.POST:
            messages.error(request, 'Puffer is only available in the US.')

            redirect_to = request.POST.get('next')
            if redirect_to:
                return redirect(reverse('login') + '?next=' + redirect_to)
            else:
                return redirect('login')

    return auth_views.login(request)


def signup(request):
    # prevent logged in user from signing up
    if request.user.is_authenticated:
        return redirect('index')

    if request.method == 'POST':
        form = SignUpForm(request.POST)

        if form.is_valid():
            invite_token = form.cleaned_data.get('invite_token')
            try:
                matching_token = InvitationToken.objects.get(token=invite_token)
            except ObjectDoesNotExist:
                messages.error(request, 'Failed to validate the provided token.')
                return redirect('signup')

            # save the user if the token is validated
            user = form.save()

            # create "addon_cnt" tokens with addon_cnt=0
            if matching_token.addon_cnt:
                for _ in range(matching_token.addon_cnt):
                    InvitationToken.objects.create(
                            token=random_token(), holder=user)

            # delete used tokens
            matching_token.delete()

            messages.success(request,
                'Your account has been created successfully! Please log in.')
            return redirect('login')
    else:
        form = SignUpForm()

    return render(request, 'accounts/signup.html', {'form': form})
