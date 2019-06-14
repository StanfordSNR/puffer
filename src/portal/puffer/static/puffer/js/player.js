'use strict';

function load_script(script_path) {
  /* Create and append a new script */
  var new_script = document.createElement('script');
  new_script.type = 'text/javascript';
  new_script.src = script_path;
  document.getElementsByTagName('head')[0].appendChild(new_script);
  return new_script;
}

function ControlBar() {
  var video = document.getElementById('tv-video');
  var tv_container = document.getElementById('tv-container');
  var tv_controls = document.getElementById('tv-controls');

  var volume_bar = document.getElementById('volume-bar');
  var mute_button = document.getElementById('mute-button');
  var unmute_here = document.getElementById('unmute-here');
  const volume_on_img = 'url(/static/puffer/dist/images/volume-on.svg)';
  const volume_off_img = 'url(/static/puffer/dist/images/volume-off.svg)';

  /* video is muted by default */
  video.volume = 0;
  mute_button.muted = true;
  mute_button.style.backgroundImage = volume_off_img;
  var last_volume_before_mute = 1;

  /* after setting the initial volume, don't manually set video.volume anymore;
   * call the function below instead */
  function set_video_volume(new_volume) {
    var unmute_message = document.getElementById('unmute-message');
    if (unmute_message) {
      unmute_message.style.display = 'none';
    }
    new_volume = Math.min(Math.max(0, new_volume.toFixed(2)), 1);

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

  const LOWERCASE_F = 70;
  const UPPERCASE_F = 102;
  const LEFT_ARROW = 37;
  const RIGHT_ARROW = 39;

  this.onkeydown = function(e) {
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
    }
  };
}

function ChannelBar() {
  var that = this;

  /* callback function to be defined when channel is changed */
  this.on_channel_change = null;

  /* find the current channel */
  var channel_list = document.querySelectorAll('#channel-list .list-group-item');
  var active_idx = 0;  // index of the active channel

  /* restore the previously watched channel if there is any */
  if (window.name) {
    var prev_channel = window.name;
    for (var i = 0; i < channel_list.length; i++) {
      if (channel_list[i].getAttribute('name') === prev_channel) {
        active_idx = i;
        break;
      }
    }
  }

  channel_list[active_idx].classList.add('active');

  var curr_channel_name = channel_list[active_idx].getAttribute('name');
  console.log('Initial channel:', curr_channel_name);
  window.name = curr_channel_name;  // save current channel in window.name

  this.get_curr_channel = function() {
    return curr_channel_name;
  };

  function change_channel(new_channel_idx) {
    if (new_channel_idx >= channel_list.length || new_channel_idx < 0) {
      return;  // invalid channel index
    }

    if (active_idx === new_channel_idx) {
      return;  // same channel
    }

    var old_channel = channel_list[active_idx];
    var new_channel = channel_list[new_channel_idx];

    old_channel.classList.remove('active');
    new_channel.classList.add('active');

    curr_channel_name = new_channel.getAttribute('name');
    console.log('Set channel:', curr_channel_name);
    window.name = curr_channel_name;  // save current channel in window.name

    if (that.on_channel_change) {
      /* call on_channel_change callback */
      that.on_channel_change(curr_channel_name);
    } else {
      console.log('Warning: on_channel_change callback has not been defined');
    }

    active_idx = new_channel_idx;
  }

  /* set up onclick callbacks for channels */
  for (var i = 0; i < channel_list.length; i++) {
    channel_list[i].onclick = (function(i) {
      return function() {
        change_channel(i);
      };
    })(i);
  }

  const UP_ARROW = 38;
  const DOWN_ARROW = 40;
  this.onkeydown = function(e) {
    if (e.keyCode === DOWN_ARROW) {
      change_channel(active_idx + 1);
    } else if (e.keyCode === UP_ARROW) {
      change_channel(active_idx - 1);
    }
  };
}


function get_client_system_info() {
  /* Below code adapted from https://stackoverflow.com/a/18706818 */
  var nAgt = navigator.userAgent;
  var browser = navigator.appName;
  var nameOffset, verOffset;

  // Opera
  if ((verOffset = nAgt.indexOf('Opera')) != -1) {
    browser = 'Opera';
  }
  // Opera Next
  else if ((verOffset = nAgt.indexOf('OPR')) != -1) {
    browser = 'Opera';
  }
  // Edge
  else if ((verOffset = nAgt.indexOf('Edge')) != -1) {
    browser = 'Microsoft Edge';
  }
  // MSIE
  else if ((verOffset = nAgt.indexOf('MSIE')) != -1) {
    browser = 'Microsoft Internet Explorer';
  }
  // Chrome
  else if ((verOffset = nAgt.indexOf('Chrome')) != -1) {
    browser = 'Chrome';
  }
  // Safari
  else if ((verOffset = nAgt.indexOf('Safari')) != -1) {
    browser = 'Safari';
  }
  // Firefox
  else if ((verOffset = nAgt.indexOf('Firefox')) != -1) {
    browser = 'Firefox';
  }
  // MSIE 11+
  else if (nAgt.indexOf('Trident/') != -1) {
    browser = 'Microsoft Internet Explorer';
  }
  // Other browsers
  else if ((nameOffset = nAgt.lastIndexOf(' ') + 1)
           < (verOffset = nAgt.lastIndexOf('/'))) {
    browser = nAgt.substring(nameOffset, verOffset);
    if (browser.toLowerCase() === browser.toUpperCase()) {
      browser = navigator.appName;
    }
  }

  // OS
  var os = 'unknown';
  var clientStrings = [
    {s:'Windows 10', r:/(Windows 10.0|Windows NT 10.0)/},
    {s:'Windows 8.1', r:/(Windows 8.1|Windows NT 6.3)/},
    {s:'Windows 8', r:/(Windows 8|Windows NT 6.2)/},
    {s:'Windows 7', r:/(Windows 7|Windows NT 6.1)/},
    {s:'Windows Vista', r:/Windows NT 6.0/},
    {s:'Windows Server 2003', r:/Windows NT 5.2/},
    {s:'Windows XP', r:/(Windows NT 5.1|Windows XP)/},
    {s:'Windows 2000', r:/(Windows NT 5.0|Windows 2000)/},
    {s:'Windows ME', r:/(Win 9x 4.90|Windows ME)/},
    {s:'Windows 98', r:/(Windows 98|Win98)/},
    {s:'Windows 95', r:/(Windows 95|Win95|Windows_95)/},
    {s:'Windows NT 4.0', r:/(Windows NT 4.0|WinNT4.0|WinNT|Windows NT)/},
    {s:'Windows CE', r:/Windows CE/},
    {s:'Windows 3.11', r:/Win16/},
    {s:'Android', r:/Android/},
    {s:'Open BSD', r:/OpenBSD/},
    {s:'Sun OS', r:/SunOS/},
    {s:'Linux', r:/(Linux|X11)/},
    {s:'iOS', r:/(iPhone|iPad|iPod)/},
    {s:'Mac OS X', r:/Mac OS X/},
    {s:'Mac OS', r:/(MacPPC|MacIntel|Mac_PowerPC|Macintosh)/},
    {s:'QNX', r:/QNX/},
    {s:'UNIX', r:/UNIX/},
    {s:'BeOS', r:/BeOS/},
    {s:'OS/2', r:/OS\/2/},
    {s:'Search Bot', r:/(nuhk|Googlebot|Yammybot|Openbot|Slurp|MSNBot|Ask Jeeves\/Teoma|ia_archiver)/}
  ];
  for (var id in clientStrings) {
    var cs = clientStrings[id];
    if (cs.r.test(nAgt)) {
      os = cs.s;
      break;
    }
  }
  return {
    os: os,
    browser: browser
  };
}

function add_player_error(error_message, error_id) {
  var id = 'player-error-' + error_id;

  var li = document.getElementById(id);
  if (li) {
    return;
  }

  var ul = document.getElementById('player-error-list');

  li = document.createElement('li');
  li.setAttribute('id', id);

  var div = document.createElement('div');
  div.setAttribute('class', 'alert alert-danger');
  div.setAttribute('role', 'alert');
  div.innerHTML = error_message;

  li.append(div);
  ul.appendChild(li);
}

function remove_player_error(error_id) {
  var id = 'player-error-' + error_id;

  var li = document.getElementById(id);
  if (!li) {
    return;
  }

  var ul = document.getElementById('player-error-list');
  ul.removeChild(li);
}

/* clear all player errors */
function clear_player_errors() {
  var ul = document.getElementById('player-error-list');

  while (ul.firstChild) {
    ul.removeChild(ul.firstChild);
  }
}

/* start and display loading circle */
function start_spinner() {
  hide_play_button();
  var spinner = document.getElementById('tv-spinner');
  spinner.classList.remove('paused');
  spinner.style.display = 'block';
}

/* pause and hide loading circle */
function stop_spinner() {
  var spinner = document.getElementById('tv-spinner');
  spinner.classList.add('paused');
  spinner.style.display = 'none';
}

/* display/hide the play button, which is mutual exclusive with the spinner */
function show_play_button() {
  stop_spinner();
  var play_button = document.getElementById('tv-play-button');
  play_button.style.display = 'block';
}

function hide_play_button() {
  var play_button = document.getElementById('tv-play-button');
  play_button.style.display = 'none';
}

function get_screen_size() {
  const raw_screen_width = screen.width;
  const raw_screen_height = screen.height;

  var screen_width = Math.max(raw_screen_width, raw_screen_height);
  var screen_height = Math.min(raw_screen_width, raw_screen_height);

  return [screen_width, screen_height];
}

function init_player(params_json, csrf_token) {
  const params = JSON.parse(params_json);

  const session_key = params.session_key;
  const username = params.username;
  const settings_debug = params.debug;
  const port = params.port;

  /* assert that session_key and username exist */
  if (!session_key || !username) {
    console.log('Error: no session key or username')
    return;
  }

  /* client's system information (OS and browser) */
  const sysinfo = get_client_system_info();

  var control_bar = new ControlBar();
  var channel_bar = new ChannelBar();

  document.onkeydown = function(e) {
    e = e || window.event;
    control_bar.onkeydown(e);
    channel_bar.onkeydown(e);
  };

  load_script('/static/puffer/js/puffer.js').onload = function() {
    var ws_client = new WebSocketClient(
      session_key, username, settings_debug, port, csrf_token, sysinfo);

    channel_bar.on_channel_change = function(new_channel) {
      ws_client.set_channel(new_channel);
    };

    /* configure play button's onclick event */
    var play_button = document.getElementById('tv-play-button');
    play_button.onclick = function() {
      ws_client.set_channel(channel_bar.get_curr_channel());
      play_button.style.display = 'none';
    };

    ws_client.connect(channel_bar.get_curr_channel());
  };
}
