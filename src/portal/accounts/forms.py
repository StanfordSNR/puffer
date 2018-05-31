from django import forms
from django.contrib.auth.forms import UserCreationForm
from django.contrib.auth.models import User


class SignUpForm(UserCreationForm):
    first_name = forms.CharField(max_length=30, required=False, help_text='Optional.')
    last_name = forms.CharField(max_length=30, required=False, help_text='Optional.')
    email = forms.EmailField(max_length=254, help_text='Required. Inform a valid email address.')
    invite_token = forms.CharField(max_length=64, help_text='A valid invitation token is required for signup.')

    class Meta:
        model = User
        fields = ('username', 'first_name', 'last_name', 'email', 'invite_token', 'password1', 'password2', )
