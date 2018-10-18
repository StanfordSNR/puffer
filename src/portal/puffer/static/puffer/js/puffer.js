'use strict';

const WS_OPEN = 1;
const GLOBAL_TIMESCALE = 90000;

const TIMER_INTERVAL = 500;
const DEBUG_TIMER_INTERVAL = 500;
const BASE_RECONNECT_BACKOFF = 1000;
const MAX_RECONNECT_BACKOFF = 15000;

var debug = false;
var non_secure = false;

/* Server messages are of the form: "short_metadata_len|metadata_json|data" */
function parse_server_msg(data) {
  var header_len = new DataView(data, 0, 2).getUint16();
  return {
    metadata: JSON.parse(new TextDecoder().decode(
        data.slice(2, 2 + header_len))),
    data: data.slice(2 + header_len)
  };
}

/* Client messages are json_data */
function format_client_msg(msg_type, data) {
  data.type = msg_type;
  return JSON.stringify(data);
}

/* Concatenates an array of arraybuffers */
function concat_arraybuffers(arr, len) {
  var tmp = new Uint8Array(len);
  arr.reduce(function (i, x) {
    tmp.set(new Uint8Array(x), i);
    return i + x.byteLength;
  }, 0);
  return tmp.buffer;
}

function AVSource(video, options) {
  var that = this;

  /* SourceBuffers for audio and video */
  var vbuf = null, abuf = null;

  var channel = options.channel;
  var video_codec = options.videoCodec;
  var audio_codec = options.audioCodec;
  var timescale = options.timescale;
  var init_seek_ts = Math.max(options.initAudioTimestamp,
                              options.initVideoTimestamp);
  var init_id = options.initId;

  /* Timestamps for the next chunks that the player is expecting */
  var next_audio_timestamp = options.initAudioTimestamp;
  var next_video_timestamp = options.initVideoTimestamp;

  /* Add pending chunks to SourceBuffers only if SourceBuffers
   * are initialized and ready to accept more chunks */
  var pending_video_chunks = [];
  var pending_audio_chunks = [];

  var ms = new MediaSource();

  video.src = URL.createObjectURL(ms);
  video.load();

  /* Initialize video and audio source buffers, and set the initial offset */
  function init_source_buffers() {
    console.log('Initializing new media source buffer');

    video.currentTime = init_seek_ts / timescale;

    vbuf = ms.addSourceBuffer(video_codec);
    vbuf.addEventListener('updateend', that.vbuf_update);
    vbuf.addEventListener('error', function(e) {
      console.log('video source buffer error:', e);
      that.close();
    });
    vbuf.addEventListener('abort', function(e) {
      console.log('video source buffer abort:', e);
    });

    abuf = ms.addSourceBuffer(audio_codec);
    abuf.addEventListener('updateend', that.abuf_update);
    abuf.addEventListener('error', function(e) {
      console.log('audio source buffer error:', e);
      that.close();
    });
    abuf.addEventListener('abort', function(e) {
      console.log('audio source buffer abort:', e);
    });

    /* try updating vbuf and abuf in case there are already pending chunks */
    that.vbuf_update();
    that.abuf_update();
  }

  ms.addEventListener('sourceopen', function(e) {
    if (debug) {
      console.log('sourceopen: ' + ms.readyState, e);
    }
    init_source_buffers();
  });
  ms.addEventListener('sourceended', function(e) {
    if (debug) {
      console.log('sourceended: ' + ms.readyState, e);
    }
  });
  ms.addEventListener('sourceclose', function(e) {
    if (debug) {
      console.log('sourceclose: ' + ms.readyState, e);
    }
    that.close();
  });
  ms.addEventListener('error', function(e) {
    console.log('media source error: ' + ms.readyState, e);
    that.close();
  });

  this.canResume = function(options) {
    return (
      options.canResume
      && options.channel === channel
      && options.videoCodec === video_codec
      && options.audioCodec === audio_codec
      && options.timescale === timescale
      && options.initVideoTimestamp === next_video_timestamp
      && options.initAudioTimestamp === next_audio_timestamp
    );
  };

  this.resume = function(options) {
    init_id = options.initId;
  };

  this.isOpen = function() { return abuf !== null && vbuf !== null; };

  /* Close the AV source, presumably it is being replaced */
  this.close = function() {
    if (vbuf || abuf) {
      console.log('Closing media source buffer');
    }

    pending_audio_chunks = [];
    pending_video_chunks = [];
    abuf = null;
    vbuf = null;
  };

  var curr_video_quality = null;
  var partial_video_chunks = null;
  var curr_ssim = null;
  var curr_video_bitrate = null;

  this.getVideoQuality = function() { return curr_video_quality; };
  this.getSSIM = function() { return curr_ssim; };
  this.getVideoBitrate = function() { return curr_video_bitrate; };

  this.handleVideo = function(data, metadata) {
    if (channel !== metadata.channel) {
      console.log('ignored data from incorrect channel');
      return;
    }

    curr_ssim = metadata.ssim;

    /* New segment or server aborted sending */
    if (curr_video_quality !== metadata.quality) {
      curr_video_quality = metadata.quality;
      partial_video_chunks = [];
    }
    partial_video_chunks.push(data);

    /* Last fragment received */
    if (data.byteLength + metadata.byteOffset === metadata.totalByteLength) {
      if (debug) {
        console.log('video: done receiving', metadata.timestamp);
      }

      pending_video_chunks.push({
        ts: metadata.timestamp,
        data: concat_arraybuffers(partial_video_chunks,
                                  metadata.totalByteLength)
      });
      partial_video_chunks = [];

      next_video_timestamp = metadata.timestamp + metadata.duration;
      curr_video_bitrate = 0.001 * 8 * metadata.totalByteLength /
                           (metadata.duration / GLOBAL_TIMESCALE);  // kbps

      /* try updating vbuf */
      that.vbuf_update();
    } else if (debug) {
      console.log('video: not done receiving', metadata.timestamp);
    }
  };

  var curr_audio_quality = null;
  var partial_audio_chunks = null;

  this.getAudioQuality = function() { return curr_audio_quality; }

  this.handleAudio = function(data, metadata) {
    if (channel !== metadata.channel) {
      console.log('ignored data from incorrect channel');
      return;
    }

    /* New segment or server aborted sending */
    if (curr_audio_quality !== metadata.quality) {
      curr_audio_quality = metadata.quality;
      partial_audio_chunks = [];
    }
    partial_audio_chunks.push(data);

    /* Last fragment received */
    if (data.byteLength + metadata.byteOffset === metadata.totalByteLength) {
      if (debug) {
        console.log('audio: done receiving', metadata.timestamp);
      }

      pending_audio_chunks.push({
        ts: metadata.timestamp,
        data: concat_arraybuffers(partial_audio_chunks,
                                  metadata.totalByteLength)
      });
      partial_audio_chunks = [];

      next_audio_timestamp = metadata.timestamp + metadata.duration;

      /* try updating abuf */
      that.abuf_update();
    } else if (debug) {
      console.log('audio: not done receiving', metadata.timestamp);
    }
  };

  /* Log debugging info to console */
  this.logBufferInfo = function() {
    if (video.buffered.length > 1) {
      console.log('Error: video.buffered.length=' + video.buffered.length +
                  ', server not sending segments in order?');
    }

    for (var i = 0; i < video.buffered.length; i++) {
      // There should only be one range if the server is sending in order
      console.log('video range:',
                  video.buffered.start(i), '-', video.buffered.end(i));
    }
  };

  this.getChannel = function() {
    return channel;
  }

  /* Get the number of seconds of video buffered */
  this.getVideoBufferLen = function() {
    if (video.buffered.length > 0) {
      return video.buffered.end(0) - video.currentTime;
    } else {
      return -1;
    }
  };

  /* Use video buffer length for the audio */
  this.getAudioBufferLen = function() {
    return that.getVideoBufferLen();
  };

  /* Get the expected timestamp of the next video chunk */
  this.getNextVideoTimestamp = function() {
    return next_video_timestamp;
  };

  /* Get the expected timestamp of the next audio chunk */
  this.getNextAudioTimestamp = function() {
    return next_audio_timestamp;
  };

  /* Get the id that identifies this initialized source */
  this.getInitId = function() {
    return init_id;
  };

  /* Pushes data onto the SourceBuffers if they are ready */
  this.vbuf_update = function() {
    if (vbuf && !vbuf.updating && pending_video_chunks.length > 0) {
      var next_video = pending_video_chunks.shift();
      vbuf.appendBuffer(next_video.data);
    }
  }

  this.abuf_update = function() {
    if (abuf && !abuf.updating && pending_audio_chunks.length > 0) {
      var next_audio = pending_audio_chunks.shift();
      abuf.appendBuffer(next_audio.data);
    }
  };
}

function WebSocketClient(video, session_key, username) {
  var that = this;

  var ws = null;
  var av_source = null;

  /* Exponential backoff to reconnect */
  var rc_backoff = BASE_RECONNECT_BACKOFF;
  var last_open = null;
  var last_received_ts = null;

  var os = null;
  var browser = null;

  function send_client_init(ws, channel) {
    if (!(ws && ws.readyState === WS_OPEN)) {
      return;
    }

    try {
      if (os === null ||  browser === null) {
        const client_info = get_client_system_info();
        os = client_info.os;
        browser = client_info.browser;
      }

      var msg = {
        sessionKey: session_key,
        userName: username,
        channel: channel,
        os: os,
        browser: browser,
        screenHeight: screen.height,
        screenWidth: screen.width
      };

      if (av_source && av_source.getChannel() === channel) {
        msg.nextAudioTimestamp = av_source.getNextAudioTimestamp();
        msg.nextVideoTimestamp = av_source.getNextVideoTimestamp();
      }

      ws.send(format_client_msg('client-init', msg));

      if (debug) {
        console.log('sent client-init', msg);
      }
    } catch (e) {
      console.log(e);
    }
  }

  function send_client_info(event, extra_payload = {}) {
    if (!(ws && ws.readyState === WS_OPEN)) {
      return;
    }

    if (!(av_source && av_source.isOpen())) {
      return;
    }

    try {
      var payload = {
        event: event,
        videoBufferLen: av_source.getVideoBufferLen(),
        audioBufferLen: av_source.getAudioBufferLen(),
        nextVideoTimestamp: av_source.getNextVideoTimestamp(),
        nextAudioTimestamp: av_source.getNextAudioTimestamp(),
        playerReadyState: video.readyState,
        initId: av_source.getInitId(),
        screenHeight: screen.height,
        screenWidth: screen.width
      };

      if (extra_payload) {
        for (var ele in extra_payload) {
          payload[ele] = extra_payload[ele];
        }
      }

      ws.send(format_client_msg('client-info', payload));
    } catch (e) {
      console.log('Failed to send client info', e);
    }
  }

  /* Handle a websocket message from the server */
  function handle_ws_msg(e) {
    var message = parse_server_msg(e.data);

    if (message.metadata.type === 'server-init') {
      if (debug) {
        console.log(message.metadata.type, message.metadata);
      }

      /* return if client is able to resume */
      if (av_source && av_source.isOpen() &&
          av_source.canResume(message.metadata)) {
        console.log('Resuming playback');
        av_source.resume(message.metadata);
        return;
      }

      /* create a new AVSource if it does not exist or unable to resume */
      if (av_source) {
        av_source.close();
      }
      av_source = new AVSource(video, message.metadata);
    } else if (message.metadata.type === 'server-audio') {
      if (debug) {
        console.log('received', message.metadata.type,
                    message.metadata.timestamp,
                    message.metadata.quality);
      }

      if (!av_source) {
        console.log('Error: AVSource is not initialized yet');
        return;
      }

      /* note: handleAudio can buffer chunks even if !av_source.isOpen() */
      av_source.handleAudio(message.data, message.metadata);
      send_client_info('audack', message.metadata);
    } else if (message.metadata.type === 'server-video') {
      if (debug) {
        console.log('received', message.metadata.type,
                    message.metadata.timestamp,
                    message.metadata.quality);
      }

      if (!av_source) {
        console.log('Error: AVSource is not initialized yet');
        return;
      }

      /* note: handleVideo can buffer chunks even if !av_source.isOpen() */
      av_source.handleVideo(message.data, message.metadata);

      /* estimate receiving time */
      var receiving_time_ms = 0;
      var received_bytes = message.data.byteLength;
      if (last_received_ts) {
        receiving_time_ms = e.timeStamp - last_received_ts;
      }

      send_client_info('vidack',
          {...message.metadata, receiving_time_ms, received_bytes});
    } else {
      console.log('received unknown message', message.metadata.type);
    }

    last_received_ts = e.timeStamp;
  }

  this.connect = function(channel) {
    const ws_host_port = location.hostname + ':9361';
    const ws_addr = non_secure ? 'ws://' + ws_host_port
                               : 'wss://' + ws_host_port;
    ws = new WebSocket(ws_addr);

    ws.binaryType = 'arraybuffer';
    ws.onmessage = handle_ws_msg;

    ws.onopen = function(e) {
      console.log('Connected to', ws_addr);
      last_open = Date.now();

      send_client_init(ws, channel);
    };

    ws.onclose = function(e) {
      console.log('Closed connection to', ws_addr);
      ws = null;

      /* reset rc_backoff if WebSocket has been open for a while */
      if (last_open && Date.now() - last_open > MAX_RECONNECT_BACKOFF) {
        rc_backoff = BASE_RECONNECT_BACKOFF;
      }

      last_open = null;

      if (rc_backoff <= MAX_RECONNECT_BACKOFF) {
        /* Try to reconnect */
        console.log('Reconnecting in ' + rc_backoff + 'ms');

        setTimeout(function() {
          if (av_source) {
            that.connect(av_source.getChannel());
          } else {
            that.connect(channel);
          }
        }, rc_backoff);

        rc_backoff = rc_backoff * 2;
      }
    };

    ws.onerror = function(e) {
      console.log('WebSocket error:', e);
      ws = null;
    };
  };

  this.set_channel = function(channel) {
    send_client_init(ws, channel);
  };

  video.oncanplay = function() {
    console.log('Video can play');
    send_client_info('canplay');
  };

  video.onwaiting = function() {
    console.log('Video is rebuffering');
    send_client_info('rebuffer');
  };

  // Start sending status updates to the server
  function timer_helper() {
    send_client_info('timer');
    setTimeout(timer_helper, TIMER_INTERVAL);
  }
  timer_helper();

  const na = 'N/A';
  function debug_timer_helper() {
    if (av_source && av_source.isOpen()) {
      var video_buf = document.getElementById('video-buf');
      const vbuf_val = av_source.getVideoBufferLen();
      video_buf.innerHTML = vbuf_val >= 0 ? vbuf_val.toFixed(1) : na;

      var video_res = document.getElementById('video-res');
      var video_crf = document.getElementById('video-crf');
      var vqual_val = av_source.getVideoQuality();
      if (vqual_val) {
        const [vres_val, vcrf_val] = vqual_val.split('-');
        video_res.innerHTML = vres_val;
        video_crf.innerHTML = vcrf_val;
      } else {
        video_res.innerHTML = na;
        video_crf.innerHTML = na;
      }

      var video_ssim = document.getElementById('video-ssim');
      const vssim_val = av_source.getSSIM();
      video_ssim.innerHTML = vssim_val ? vssim_val.toFixed(2) : na;

      var video_bitrate = document.getElementById('video-bitrate');
      const vbitrate_val = av_source.getVideoBitrate();
      video_bitrate.innerHTML = vbitrate_val ? vbitrate_val.toFixed(2) : na;
    }
    setTimeout(debug_timer_helper, DEBUG_TIMER_INTERVAL);
  }
  debug_timer_helper();
}

function get_client_system_info() {
  /* Below code adapted from https://github.com/keithws/browser-report */
  var nAgt = navigator.userAgent;
  var browser = navigator.appName;
  var nameOffset, verOffset;

  // Opera
  if ((verOffset = nAgt.indexOf('Opera')) != -1) {
    browser = 'Opera';
  }
  // Opera Next
  if ((verOffset = nAgt.indexOf('OPR')) != -1) {
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

  // system
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

function start_puffer(session_key, username, settings_debug) {
  /* if DEBUG = True in settings.py, connect to non-secure WebSocket server */
  non_secure = settings_debug;

  const video = document.getElementById('tv-video');

  const client = new WebSocketClient(video, session_key, username);
  return client;
}
