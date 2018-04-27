const abr_id = 3;
const manifest_url = "other_algorithms/nbc-clip/live.mpd";

function start_streaming(user) {
  var player = dashjs.MediaPlayer().create();
  var abr_algorithms = {
    0: 'Default',
    1: 'Fixed Rate (0)',
    2: 'Buffer Based',
    3: 'Rate Based',
    4: 'RL',
    5: 'Festive',
    6: 'Bola'
  };

  // use the below block if you want to change the buffer size that dash tries to maintain
  /* player.setBufferTimeAtTopQuality(60);
   * player.setStableBufferTime(60);
   * player.setBufferToKeep(60);
   * player.setBufferPruningInterval(60); */

  player.initialize(document.getElementById('tv-player'), manifest_url, true);
  player.clearDefaultUTCTimingSources();

  if (abr_id === 6) { /* BOLA */
    player.enableBufferOccupancyABR(true);
  } else if (abr_id > 1) {
    player.enablerlABR(true);
  }

  player.setAbrAlgorithm(abr_id);
}

function initApp() {
  /* Listening for auth state changes */
  firebase.auth().onAuthStateChanged(function(user) {
    if (user) {
      document.getElementById('user-signed-in').style.display = 'block';
      document.getElementById('user-info').textContent = 'Welcome! ' + user.displayName;

      /* User is signed in */
      start_streaming(user);
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
};

window.addEventListener('load', initApp);
