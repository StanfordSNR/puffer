from django import forms
from django.contrib.auth.forms import UserCreationForm
from django.contrib.auth.models import User
from accounts.models import InvitationToken
import re


class InviteTokenField(forms.CharField):
    def validate(self, value):
        super().validate(value)  # Use normal charField validator first

        if not InvitationToken.objects.filter(token=value).exists():
            # No matching invitation code was found
            raise forms.ValidationError("Provide a valid invitation code")


class SignUpForm(UserCreationForm):
    class Meta:
        model = User
        fields = ('username', 'password1', 'password2')

    def clean_username(self):
        username = self.cleaned_data.get('username')

        if not re.match(r'^[A-Za-z0-9_-]+$', username):
            raise forms.ValidationError(
                "The username must contain only alphanumeric, dash, "
                "and underscore characters.")

        if not (len(username) >= 3 and len(username) <= 30):
            raise forms.ValidationError(
                "The username must be between 3 and 30 characters long."
            )

        if User.objects.filter(username=username).exists():
            raise forms.ValidationError(
                "The username is already in use."
            )

        return username
