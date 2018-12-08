from django import forms
from django.contrib.auth.forms import UserCreationForm
from django.contrib.auth.models import User
from accounts.models import InvitationToken
from django.utils.translation import ugettext_lazy as _
import re


class InviteTokenField(forms.CharField):
    def validate(self, value):
        super().validate(value)  # Use normal charField validator first

        if not InvitationToken.objects.filter(token=value).exists():
            # No matching invitation code was found
            raise forms.ValidationError("Provide a valid invitation code")


class SignUpForm(UserCreationForm):
    error_messages = {
        'password_mismatch': _("The two password fields didn't match."),
        'username_invalid': _(
            "The username must be at least 5 characters in length and "
            "must contain only alphanumeric, dash, and underscore characters."),
    }

    class Meta:
        model = User
        fields = ('username', 'password1', 'password2')

    def clean_username(self):
        username = self.cleaned_data.get("username")

        if not (len(username) >= 5 and re.match('^[\w-]+$', username)):
            raise forms.ValidationError(
                self.error_messages['username_invalid'],
                code='username_invalid',
            )
        return username
