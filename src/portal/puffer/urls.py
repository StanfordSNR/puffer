from django.urls import path
from django.views.generic import TemplateView

urlpatterns = [
    path('', TemplateView.as_view(template_name='index.html')),
    path('index_dev.html', TemplateView.as_view(template_name='index_dev.html')),
    path('player.html', TemplateView.as_view(template_name='player.html')),
]
