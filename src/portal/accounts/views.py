#from django.contrib.auth.forms import UserCreationForm
#from django.urls import reverse_lazy
#from django.views import generic

from django.contrib.auth import login, authenticate
from django.shortcuts import render, redirect
from django.contrib import messages
from accounts.models import tokenStorageModel

from accounts.forms import SignUpForm

def SignUp(request):
    if request.method == 'POST':
        form = SignUpForm(request.POST)
        if form.is_valid():
            form.save()
            username = form.cleaned_data.get('username')
            raw_password = form.cleaned_data.get('password1')
            #Note that form.is_valid() will only ever be true if a valid token was provided
            #Thus, a matchingToken is gauranteed to exist

            invite_token = form.cleaned_data.get('invite_token')
            matchingToken = tokenStorageModel.objects.filter(token=invite_token)
            matchingToken.delete() # Now that we have created a new user, delete their invite token
                                   # from the list of unassigned tokens
            user = authenticate(username=username, password=raw_password)
            login(request, user)
            return redirect('index')
    else:
        form = SignUpForm()
    return render(request, 'accounts/signup.html', {'form': form})

#class SignUp(generic.CreateView):
#    form_class = UserCreationForm
#    success_url = reverse_lazy('login')
#    template_name = 'accounts/signup.html'
