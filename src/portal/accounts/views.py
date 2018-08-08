from django.contrib.auth import login, authenticate, REDIRECT_FIELD_NAME
from django.contrib.auth import views as auth_views
from django.shortcuts import render, redirect
from django.contrib import messages
from accounts.models import InvitationToken
from accounts.forms import SignUpForm


def my_login(request):
    if request.method == 'POST':
        if 'america' not in request.POST:
            messages.warning(request, 'Puffer is only available \
                                       in the United States')
            # Next few lines ensure redirect path is not lost
            redirect_to = request.POST.get(
                REDIRECT_FIELD_NAME,
                request.GET.get(REDIRECT_FIELD_NAME, '')
            )
            response = redirect('login')
            response['Location'] += '?next=' + str(redirect_to)
            return response
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

            # log in the new user
            username = form.cleaned_data.get('username')
            raw_password = form.cleaned_data.get('password1')
            user = authenticate(username=username, password=raw_password)
            login(request, user)

            return redirect('index')
    else:
        form = SignUpForm()

    return render(request, 'accounts/signup.html', {'form': form})
