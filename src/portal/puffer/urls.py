from django.urls import path
from . import views

urlpatterns = [
    path('', views.index, name='index'),
    path('profile/', views.profile, name='profile'),
    path('player/<int:aid>/', views.player, name='player'),
    path('rating/', views.rating, name='rating'),
    path('monitoring/', views.monitoring, name='monitoring'),
]
