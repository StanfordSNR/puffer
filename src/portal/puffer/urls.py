from django.urls import path

from . import views

urlpatterns = [
    path('', views.index, name='index'),
    path('player/<int:aid>/', views.player, name='player'),
]
