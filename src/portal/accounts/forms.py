from django import forms
from django.contrib.auth.forms import UserCreationForm
from django.contrib.auth.models import User
from accounts.models import InvitationToken


class InviteTokenField(forms.CharField):
    def validate(self, value):
        super().validate(value)  # Use normal charField validator first

        if not InvitationToken.objects.filter(token=value).exists():
            # No matching invitation code was found
            raise forms.ValidationError("Provide a valid invitation code")


class SignUpForm(UserCreationForm):
    email = forms.EmailField(max_length=254,
                             help_text='Required. Inform a valid\
                                        email address.')
    invite_token = InviteTokenField(max_length=64,
                                    help_text='A valid invitation token\
                                    is required for signup.')

    class Meta:
        model = User
        fields = ('username', 'email', 'invite_token', 'password1',
                  'password2', )
