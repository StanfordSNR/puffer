from django.db import models
from django.contrib.auth.models import User


class InvitationToken(models.Model):
    token = models.CharField(max_length=64)
    holder = models.ForeignKey(User, on_delete=models.CASCADE,
                               null=True, blank=True)

    def __str__(self):
        if self.holder:
            return self.token + " (" + self.holder.username + ")"
        else:
            return self.token + " (unassigned)"
