from django.contrib.auth import login, authenticate
from django.shortcuts import render, redirect
from django.contrib import messages
from accounts.models import InvitationToken
from accounts.forms import SignUpForm


def signup(request):
    if request.method == 'POST':
        form = SignUpForm(request.POST)

        if form.is_valid():
            form.save()

            # delete invitation token after the new user is created
            invite_token = form.cleaned_data.get('invite_token')
            matching_token = InvitationToken.objects.filter(token=invite_token)
            if matching_token:
                matching_token.delete()

            # log in the new user
            username = form.cleaned_data.get('username')
            raw_password = form.cleaned_data.get('password1')
            user = authenticate(username=username, password=raw_password)
            login(request, user)

            return redirect('index')
    else:
        form = SignUpForm()

    return render(request, 'accounts/signup.html', {'form': form})
