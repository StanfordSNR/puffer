from django.db import models

# Create your models here.

class InvitationToken(models.Model):
    token = models.CharField(max_length=64, default='0')

# To add tokens log directly into the postgres database and add tokens
