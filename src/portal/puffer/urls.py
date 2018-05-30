from django.urls import path

from puffer import views


urlpatterns = [
    path('', views.index, name='index'),
    path('player/<int:aid>/', views.player, name='player'),
    path('login/', views.login, name='login'),
]
