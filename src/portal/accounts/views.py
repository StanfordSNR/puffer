#from django.contrib.auth.forms import UserCreationForm
#from django.urls import reverse_lazy
#from django.views import generic

from django.contrib.auth import login, authenticate
from django.shortcuts import render, redirect

from accounts.forms import SignUpForm

def SignUp(request):
    if request.method == 'POST':
        form = SignUpForm(request.POST)
        if form.is_valid():
            form.save()
            username = form.cleaned_data.get('username')
            raw_password = form.cleaned_data.get('password1')
            invite_token = form.cleaned_data.get('invite_token')
            if invite_token != '63':
                print("INVALID TOKEN: " + invite_token)
                return redirect('account/signup.html')
            user = authenticate(username=username, password=raw_password)
            login(request, user)
            return redirect('login')
    else:
        form = SignUpForm()
    return render(request, 'accounts/signup.html', {'form': form})

#class SignUp(generic.CreateView):
#    form_class = UserCreationForm
#    success_url = reverse_lazy('login')
#    template_name = 'accounts/signup.html'
