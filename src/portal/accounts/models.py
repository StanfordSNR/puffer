from django.db import models


class InvitationToken(models.Model):
    token = models.CharField(max_length=64, default=None)

# To add tokens log directly into the postgres database and add tokens
