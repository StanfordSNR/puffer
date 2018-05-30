function rand_alg() {
  var rand_urls = [
    "../player.html?aid=1",
    "../player.html?aid=2",
    "../player.html?aid=3",
    "../player.html?aid=4",
    "../player.html?aid=5",
    "../player.html?aid=6",
    "../player.html?aid=7",
    "../player.html?aid=8",
    "../player.html?aid=10",
    "../player.html?aid=11"
  ];

  var rand_url = rand_urls[Math.floor(Math.random() * rand_urls.length)];
  window.location.replace(rand_url);
}

function init_app() {

}
