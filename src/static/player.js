function get_parameter_by_name(name, url) {
  if (!url) url = window.location.href;
  name = name.replace(/[\[\]]/g, "\\$&");
  var regex = new RegExp("[?&]" + name + "(=([^&#]*)|&|#|$)");
  var results = regex.exec(url);
  if (!results) return null;
  if (!results[2]) return '';
  return decodeURIComponent(results[2].replace(/\+/g, " "));
}

function load_script(script_path) {
  /* Create and append a new script */
  var new_script = document.createElement('script');
  new_script.type = 'text/javascript';
  new_script.src = script_path;
  document.getElementsByTagName('head')[0].appendChild(new_script);
  return new_script;
}

function start_dashjs(user, aid) {
  const channel_select = document.getElementById('channel-select');
  var manifest_url = '/media/' + channel_select.value + '/ready/live.mpd';

  var player = dashjs.MediaPlayer().create();
  player.initialize(document.getElementById("tv-player"), manifest_url, true);
  player.clearDefaultUTCTimingSources();

  channel_select.onchange = function() {
    console.log('set channel:', channel_select.value);
    player.attachSource('/media/' + channel_select.value + '/ready/live.mpd');
  };

  if (aid === 2) {  // default dash.js
  } else if (aid === 3) {  // BOLA dash.js
    player.setABRStrategy('abrBola');
  } else {
    /* Uncomment this block if you want to change the buffer size
       that dash.js tries to maintain */
    /*
    player.setBufferTimeAtTopQuality(60);
    player.setStableBufferTime(60);
    player.setBufferToKeep(60);
    player.setBufferPruningInterval(60);
    */

    /* algorithm IDs in pensieve:
      1: 'Fixed Rate'
      2: 'Buffer Based'
      3: 'Rate Based'
      4: 'Pensieve'
      5: 'Festive'
      6: (occupied)
      7: 'FastMPC
      8: 'RobustMPC' */
    pensieve_abr_id = aid - 3;

    if (pensieve_abr_id > 1) {
      player.enablerlABR(true);
    }

    player.setAbrAlgorithm(pensieve_abr_id);
  }
}

function setup_control_bar() {
  const video = document.getElementById('tv-player');
  const mute_button = document.getElementById('mute-button');
  const volume_bar = document.getElementById('volume-bar');
  const full_screen_button = document.getElementById('full-screen-button');

  var last_volume_before_mute = video.volume;

  mute_button.onclick = function() {
    
    if (mute_button.muted == true) {
       video.volume = last_volume_before_mute;
       volume_bar.value = video.volume;
       mute_button.muted = false;
       mute_button.style.backgroundImage = "url(/images/volume_on.svg)";
   

    } else {
        mute_button.muted = true;
        last_volume_before_mute = video.volume;
        video.volume = 0;
        volume_bar.value = 0;
        mute_button.style.backgroundImage = "url(/images/volume_off.svg)";
    } 
  };
/*  toggleMuteBtnState = function () {
      var span = document.getElementById(getControlId('iconMute'));
      if (player.isMuted()) {
          span.classList.remove('icon-mute-off');
          span.classList.add('icon-mute-on');
      } else {
          span.classList.remove('icon-mute-on')
          span.classList.add('icon-mute-off');
      }
  };

  onMuteClick = function (e) {
      if (player.isMuted() && !isNaN(lastVolumeLevel)) {
          setVolume(lastVolumeLevel);
      } else {
          lastVolumeLevel = parseFloat(volumebar.value);
          setVolume(0);
      }
      player.setMute(player.getVolume() === 0);
      toggleMuteBtnState();
  }; */

  volume_bar.value = video.volume;
  volume_bar.onchange = function() {
    video.volume = volume_bar.value;
  };

  full_screen_button.onclick = function() {
    if (video.requestFullscreen) {
      video.requestFullscreen();
    } else if (video.mozRequestFullScreen) {
      video.mozRequestFullScreen();
    } else if (video.webkitRequestFullscreen) {
      video.webkitRequestFullscreen();
    }
  };
}

function init_app() {
  /* Listening for auth state changes */
  firebase.auth().onAuthStateChanged(function(user) {
    if (user) {
      /* User is signed in */
      document.getElementById('user-signed-in').style.display = 'block';
      document.getElementById('user-info').textContent = 'Welcome! ' + user.displayName;

      /* Set up the player control bar */
      setup_control_bar();

      /* Get algorithm ID from the URL */
      var aid = Number(get_parameter_by_name('aid'));  // algorithm ID

      if (aid === 1) {  // puffer
        load_script('puffer.js').onload = function() {
          start_puffer(user);  // start_puffer is defined in puffer.js
        }
      } else {
        /* All the other algorithms are based on dash.js */
        var new_script = null;

        if (aid === 2 || aid === 3) {  // algorithms available in dash.js
          new_script = load_script('dist/dash.all.min.js');
        } else if (aid >= 4 && aid <= 11) {  // algorithms available in pensieve
          new_script = load_script('dist/pensieve.dash.all.min.js');
        }

        new_script.onload = function() {
          start_dashjs(user, aid);
        }
      }
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
