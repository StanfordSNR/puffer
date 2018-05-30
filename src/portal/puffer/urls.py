from django.urls import path
from django.views.generic import TemplateView

urlpatterns = [
    path('', TemplateView.as_view(template_name='puffer/index.html')),
    path('index_dev.html', TemplateView.as_view(template_name='puffer/index_dev.html')),
    path('player.html', TemplateView.as_view(template_name='puffer/player.html')),
]
