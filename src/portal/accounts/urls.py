from django.urls import path
from . import views
from django.contrib.auth.views import LoginView


urlpatterns = [
    path('signup/', views.signup, name='signup'),
    path('login/', LoginView.as_view(redirect_authenticated_user=True),
         name='login'),
    path('share_token/', views.share_token, name='share_token'),
]
