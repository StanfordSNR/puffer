function rand_alg() {
  var rand_urls = [
    "/player/1/",
    "/player/2/",
    "/player/3/",
    "/player/4/",
    "/player/5/",
    "/player/6/",
    "/player/7/",
    "/player/8/",
    "/player/10/",
    "/player/11/"
  ];

  var rand_url = rand_urls[Math.floor(Math.random() * rand_urls.length)];
  window.location.replace(rand_url);
}

function init_app() {
  document.getElementById('play-rand-alg').addEventListener('click', rand_alg);
}

window.addEventListener('load', init_app);
