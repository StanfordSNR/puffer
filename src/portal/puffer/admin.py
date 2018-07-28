from django.contrib import admin

from .models import Comment, StarRating

# Register your models here.
admin.site.register(Comment)
admin.site.register(StarRating)
