from django import forms
from django.contrib.auth.forms import UserCreationForm
from django.contrib.auth.models import User
from accounts.models import InvitationToken

class InviteTokenField(forms.CharField):
    def validate(self, value):
        super().validate(value)  # Use normal charField validator first

        if not InvitationToken.objects.filter(token=value).exists():
            # No matching invitation code was found
            raise forms.ValidationError("Please provide a valid invitation code!")




class SignUpForm(UserCreationForm):
    first_name = forms.CharField(max_length=30, required=False, help_text='Optional.')
    last_name = forms.CharField(max_length=30, required=False, help_text='Optional.')
    email = forms.EmailField(max_length=254, help_text='Required. Inform a valid email address.')
    invite_token = InviteTokenField(max_length=64, help_text='A valid invitation token is required for signup.')

    class Meta:
        model = User
        fields = ('username', 'first_name', 'last_name', 'email', 'invite_token', 'password1', 'password2', )

