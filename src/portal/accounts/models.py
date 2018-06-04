from django.db import models
from django.contrib.auth.models import User


class InvitationToken(models.Model):
    token = models.CharField(max_length=64)
    holder = models.ForeignKey(User, on_delete=models.CASCADE,
                               null=True, blank=True)

    def __str__(self):
        holder = "Unassigned"
        admin = ""
        if self.holder:
            holder = self.holder.email
            admin = self.holder.is_superuser
            if admin:
                return self.token + " (" + holder + " - admin)"
            else:
                return self.token + " (" + holder + " - non-admin)"
        return self.token + " (" + holder + ")"
