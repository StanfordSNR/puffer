from django.db import models
from django.contrib.auth.models import User


class InvitationToken(models.Model):
    token = models.CharField(max_length=64, unique=True)
    holder = models.ForeignKey(User, on_delete=models.CASCADE,
                               null=True, blank=True)
    # when a user registers an account with this token, the new account will
    # be assigned and hold "addon_cnt" extra tokens for distribution
    addon_cnt = models.PositiveIntegerField(default=0)
    shared = models.BooleanField(default=False)

    def __str__(self):
        holder = self.holder.username if self.holder else 'unassigned'
        return '%s (%s, %d): shared=%s' % (self.token, holder, self.addon_cnt,
                                           self.shared)
