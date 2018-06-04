from django.db import models
from django.contrib.sessions.models import Session
from django.contrib.auth.models import User
from django.contrib.auth.signals import user_logged_in


class UserProfile(models.Model):
    user = models.OneToOneField(User, on_delete=models.CASCADE,
                                primary_key=True)
    last_session_key = models.CharField(max_length=64, default='')


def user_logged_in_handler(sender, request, user, **kwargs):
    curr_session_key = request.session.session_key

    user_profile, _ = UserProfile.objects.get_or_create(user=user)
    last_session_key = user_profile.last_session_key

    # user does not have a profile or a session key previously
    if not last_session_key:
        user_profile.last_session_key = curr_session_key
        user_profile.save()
        return

    # do nothing if user's session key does not change
    if curr_session_key == last_session_key:
        return

    # delete previous session and set the new session key
    try:
        user_session = Session.objects.get(session_key=last_session_key)
    except Session.DoesNotExist:
        user_session = None

    if user_session:
        user_session.delete()

    user_profile.last_session_key = curr_session_key
    user_profile.save()


user_logged_in.connect(user_logged_in_handler)
