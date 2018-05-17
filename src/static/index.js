function rand_alg() {
  var rand_urls = [
    "/player.html?aid=1",
    "/player.html?aid=2",
    "/player.html?aid=3",
    "/player.html?aid=4",
    "/player.html?aid=5",
    "/player.html?aid=6",
    "/player.html?aid=7",
    "/player.html?aid=8",
    "/player.html?aid=10",
    "/player.html?aid=11"
  ];

  var rand_url = rand_urls[Math.floor(Math.random() * rand_urls.length)];
  window.location.replace(rand_url);
}

function init_app() {
  /* Listening for auth state changes */
  firebase.auth().onAuthStateChanged(function(user) {
    if (user) {
      /* User is signed in */
      document.getElementById('user-signed-in').style.display = 'block';
      document.getElementById('user-info').textContent = 'Welcome! ' + user.displayName;

      document.getElementById('play-rand-alg').addEventListener('click', rand_alg);
    } else {
      /* Redirect to the sign-in page if user is not signed in */
      window.location.replace('/widget.html');
    }
  });

  document.getElementById('sign-out').addEventListener('click',
    function() {
      firebase.auth().signOut();
    }
  );
}

window.addEventListener('load', init_app);
