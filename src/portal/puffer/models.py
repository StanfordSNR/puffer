from django.db import models
from django.contrib.sessions.models import Session
from django.contrib.auth.models import User
from django.contrib.auth.signals import user_logged_in
from django.core.validators import MaxValueValidator, MinValueValidator


class UserProfile(models.Model):
    user = models.OneToOneField(User, on_delete=models.CASCADE,
                                primary_key=True)
    last_session_key = models.CharField(max_length=64, default='')


class GrafanaSnapshot(models.Model):
    url = models.URLField()
    created_on = models.DateTimeField()


class Rating(models.Model):
    user = models.ForeignKey(User, on_delete=models.CASCADE)
    comment_text = models.CharField(max_length=500, default='')
    stars = models.IntegerField(
        validators=[MinValueValidator(0), MaxValueValidator(5)], default=0)
    pub_date = models.DateTimeField('date published')

    def __str__(self):
        return '{}-{}-{}'.format(self.user, self.stars, self.comment_text)


class Participate(models.Model):
    email = models.CharField(max_length=255)
    request_date = models.DateTimeField('date requested')
    sent = models.BooleanField(default=False)

    def __str__(self):
        return self.email

# classes above are not currrently used

def user_logged_in_handler(sender, request, user, **kwargs):
    curr_session_key = request.session.session_key

    if not curr_session_key:
        request.session.create()
        curr_session_key = request.session.session_key
        return

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
