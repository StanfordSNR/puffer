from django.contrib.auth import login, authenticate
from django.shortcuts import render, redirect
from django.contrib import messages
from accounts.models import InvitationToken
from accounts.forms import SignUpForm


def SignUp(request):
    if request.method == 'POST':
        form = SignUpForm(request.POST)
        if form.is_valid():
            form.save()
            username = form.cleaned_data.get('username')
            raw_password = form.cleaned_data.get('password1')
            # form.is_valid() will only ever be true if a valid
            # token was provided. Thus, a matchingToken is gauranteed to exist

            invite_token = form.cleaned_data.get('invite_token')
            matching_token = InvitationToken.objects.filter(token=invite_token)
            # We have created the new user so we can delete their invite token
            matching_token.delete()
            user = authenticate(username=username, password=raw_password)
            login(request, user)
            return redirect('index')
    else:
        form = SignUpForm()
    return render(request, 'accounts/signup.html', {'form': form})
