from django.contrib.auth import login, authenticate, REDIRECT_FIELD_NAME
from django.contrib.auth import views as auth_views
from django.shortcuts import render, redirect
from django.contrib import messages
from django.urls import reverse
from accounts.models import InvitationToken
from accounts.forms import SignUpForm


def my_login(request):
    if request.method == 'POST':
        if 'america' not in request.POST:
            messages.error(
                request, 'Puffer is only available in the United States.')

            redirect_to = request.POST.get('next')
            if redirect_to:
                return redirect(reverse('login') + '?next=' + redirect_to)
            else:
                return redirect('login')

    return auth_views.login(request)


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

            return redirect('login')
    else:
        form = SignUpForm()

    return render(request, 'accounts/signup.html', {'form': form})
