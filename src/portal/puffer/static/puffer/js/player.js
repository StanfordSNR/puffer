'use strict';

function load_script(script_path) {
  /* Create and append a new script */
  var new_script = document.createElement('script');
  new_script.type = 'text/javascript';
  new_script.src = script_path;
  document.getElementsByTagName('head')[0].appendChild(new_script);
  return new_script;
}

function setup_control_bar(ws_client) {
  var video = document.getElementById('tv-video');
  var tv_container = document.getElementById('tv-container');
  var tv_controls = document.getElementById('tv-controls');

  var volume_bar = document.getElementById('volume-bar');
  var mute_button = document.getElementById('mute-button');
  var unmute_here = document.getElementById('unmute-here');
  const volume_on_img = 'url(/static/puffer/dist/images/volume-on.svg)';
  const volume_off_img = 'url(/static/puffer/dist/images/volume-off.svg)';

  /* don't manually set video.volume; call this function instead */
  function set_video_volume(new_volume) {
    new_volume = Math.min(Math.max(0, new_volume.toPrecision(2)), 1);

    video.muted = false;  // allow unmuting and use video.volume to control
    video.volume = new_volume;

    /* change volume bar and mute button */
    volume_bar.value = new_volume;
    if (new_volume > 0) {
      mute_button.muted = false;
      mute_button.style.backgroundImage = volume_on_img;
    } else {
      mute_button.muted = true;
      mute_button.style.backgroundImage = volume_off_img;
    }
  }

  /* video is muted by default */
  var last_volume_before_mute = 1;
  set_video_volume(0);

  volume_bar.oninput = function() {
    set_video_volume(Number(volume_bar.value));
  };

  mute_button.onclick = function() {
    if (mute_button.muted) {
      set_video_volume(last_volume_before_mute);
    } else {
      last_volume_before_mute = video.volume;
      set_video_volume(0);
    }
  };

  unmute_here.onclick = function() {
    set_video_volume(1);
  };

  function show_control_bar() {
    tv_controls.style.opacity = '0.8';
    tv_container.style.cursor = 'default';
  }

  function hide_control_bar() {
    tv_controls.style.opacity = '0';
    tv_container.style.cursor = 'none';
  }

  /* initialize opacity and cursor control bar and cursor */
  show_control_bar();
  var control_bar_timeout = setTimeout(hide_control_bar, 3000);

  function show_control_bar_briefly() {
    clearTimeout(control_bar_timeout);
    show_control_bar();

    control_bar_timeout = setTimeout(function() {
      hide_control_bar();
    }, 3000);
  }

  tv_container.onmousemove = function() {
    show_control_bar_briefly();
  };

  tv_container.onmouseleave = function() {
    tv_controls.style.opacity = '0';
  };

  function toggle_full_screen() {
    show_control_bar_briefly();

    var isInFullScreen = (document.fullscreenElement && document.fullscreenElement !== null) ||
        (document.webkitFullscreenElement && document.webkitFullscreenElement !== null) ||
        (document.mozFullScreenElement && document.mozFullScreenElement !== null) ||
        (document.msFullscreenElement && document.msFullscreenElement !== null);

    if (!isInFullScreen) {
      if (tv_container.requestFullscreen) {
        tv_container.requestFullscreen();
      } else if (tv_container.mozRequestFullScreen) {
        tv_container.mozRequestFullScreen();
      } else if (tv_container.webkitRequestFullScreen) {
        tv_container.webkitRequestFullScreen();
      } else if (tv_container.msRequestFullscreen) {
        tv_container.msRequestFullscreen();
      }
    } else {
      if (document.exitFullscreen) {
        document.exitFullscreen();
      } else if (document.webkitExitFullscreen) {
        document.webkitExitFullscreen();
      } else if (document.mozCancelFullScreen) {
        document.mozCancelFullScreen();
      } else if (document.msExitFullscreen) {
        document.msExitFullscreen();
      }
    }
  }

  var full_screen_button = document.getElementById('full-screen-button');
  full_screen_button.onclick = toggle_full_screen;
  video.ondblclick = toggle_full_screen;

  /* Full Screen toggling, and key based volume/channel selection */
  const LOWERCASE_F = 70;
  const UPPERCASE_F = 102;
  const LEFT_ARROW = 37;
  const RIGHT_ARROW = 39;
  const UP_ARROW = 38;
  const DOWN_ARROW = 40;
  var channel_list = document.querySelectorAll('#channel-list .list-group-item');

  document.onkeydown = function(e) {
    e = e || window.event;

    if (e.keyCode === LOWERCASE_F || e.keyCode === UPPERCASE_F) {
      /* Fullscreen */
      toggle_full_screen();
    } else if (e.keyCode === LEFT_ARROW || e.keyCode === RIGHT_ARROW) {
      show_control_bar_briefly();

      if (e.keyCode === LEFT_ARROW) {
        set_video_volume(video.volume - 0.05);
      } else if (e.keyCode === RIGHT_ARROW) {
        set_video_volume(video.volume + 0.05);
      }
    } else if (e.keyCode === DOWN_ARROW || e.keyCode === UP_ARROW) {
      var active_channel = document.querySelectorAll('#channel-list .active')[0];
      var active_channel_name = active_channel.getAttribute('name');

      for (var i = 0; i < channel_list.length; i++) {
        var this_value = channel_list[i];
        if (this_value !== active_channel) {
          continue;
        }

        var new_channel = null;
        if (e.keyCode === DOWN_ARROW) {
          if (i < channel_list.length - 1) {
            new_channel = channel_list[i + 1];
          }
        } else if (e.keyCode === UP_ARROW) {
          if (i > 0) {
            new_channel = channel_list[i - 1];
          }
        }

        if (!new_channel) {
          continue;
        }

        active_channel.className = active_channel.className.replace(' active', '');
        new_channel.className += ' active';
        console.log('Set channel:', new_channel.innerText);
        ws_client.set_channel(new_channel.getAttribute('name'));
      }
    }
  };
}

function setup_channel_bar(ws_client) {
  /* validate checked channel count and find default channel */
  const init_active_channel = document.querySelectorAll('#channel-list .active');
  if (init_active_channel.length !== 1) {
    console.log('Error: only one channel can be selected');
    return;
  }
  const default_channel = init_active_channel[0].getAttribute('name');
  console.log('Default channel:', init_active_channel[0].innerText);

  /* set up onclick callbacks for channels */
  const channel_list = document.querySelectorAll('#channel-list .list-group-item');
  for (var i = 0; i < channel_list.length; i++) {
    channel_list[i].onclick = function() {
      const active_channel = document.querySelectorAll('#channel-list .active')[0];
      const this_value = this.getAttribute('name');

      if (this_value === active_channel.getAttribute('name')) {
        /* same channel */
        return;
      }

      active_channel.className = active_channel.className.replace(' active', '');
      this.className += ' active';

      console.log('Set channel:', this.innerText);
      ws_client.set_channel(this_value);
    }
  }

  return default_channel;
}

function init_player(params_json) {
  var params = JSON.parse(params_json);

  var session_key = params.session_key;
  var username = params.username;
  const settings_debug = params.debug;

  /* assert that session_key and username exist */
  if (!session_key || !username) {
    console.log('Error: no session key or username')
    return;
  }

  load_script('/static/puffer/js/puffer.js').onload = function() {
    /* start_puffer is defined in puffer.js */
    var ws_client = start_puffer(session_key, username, settings_debug);

    /* set up the player control bar */
    setup_control_bar(ws_client);

    const default_channel = setup_channel_bar(ws_client);
    ws_client.connect(default_channel);
  }
}
