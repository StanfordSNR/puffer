from django.contrib import admin

from .models import Rating, Participate


def mark_sent(modeladmin, request, queryset):
    queryset.update(sent=True)
mark_sent.short_description = 'Mark selected emails as sent'


class ParticipateAdmin(admin.ModelAdmin):
    list_display = ['email', 'request_date', 'sent']
    ordering = ['-request_date']
    actions = [mark_sent]


# Register your models here.
admin.site.register(Rating)
admin.site.register(Participate, ParticipateAdmin)
