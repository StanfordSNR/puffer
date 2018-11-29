'use strict';

const WS_OPEN = 1;

const BASE_RECONNECT_BACKOFF = 250;
const MAX_RECONNECT_BACKOFF = 10000;

var debug = false;
var video = document.getElementById('tv-video');

var fatal_error = false;
function set_fatal_error(error_message) {
  if (fatal_error) {
    return;
  }

  fatal_error = true;
  clear_player_errors();
  add_player_error(error_message, 'fatal');
}

/* Server messages are of the form: "short_metadata_len|metadata_json|data" */
function parse_server_msg(data) {
  var metadata_len = new DataView(data, 0, 2).getUint16(0);

  var byte_array = new Uint8Array(data);
  var raw_metadata = byte_array.subarray(2, 2 + metadata_len);
  var media_data = byte_array.subarray(2 + metadata_len);

  /* parse metadata with JSON */
  var metadata = null;
  if (window.TextDecoder) {
    metadata = JSON.parse(new TextDecoder().decode(raw_metadata));
  } else {
    /* fallback if TextDecoder is not supported on some browsers */
    metadata = JSON.parse(String.fromCharCode.apply(null, raw_metadata));
  }

  return {
    metadata: metadata,
    data: media_data
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

function AVSource(ws_client, server_init) {
  var that = this;

  /* SourceBuffers for audio and video */
  var vbuf = null;
  var abuf = null;

  var channel = server_init.channel;
  const video_codec = server_init.videoCodec;
  const audio_codec = server_init.audioCodec;
  const timescale = server_init.timescale;
  const video_duration = server_init.videoDuration;
  const audio_duration = server_init.audioDuration;
  const init_seek_ts = Math.max(server_init.initAudioTimestamp,
                                server_init.initVideoTimestamp);

  /* Timestamps for the next chunks that the player is expecting */
  var next_video_timestamp = server_init.initVideoTimestamp;
  var next_audio_timestamp = server_init.initAudioTimestamp;

  /* Add pending chunks to SourceBuffers only if SourceBuffers
   * are initialized and ready to accept more chunks */
  var pending_video_chunks = [];
  var pending_audio_chunks = [];

  var ms = null;

  if (window.MediaSource) {
    ms = new MediaSource();
  } else {
    set_fatal_error(
      'Error: your browser does not support Media Source Extensions (MSE), ' +
      'which Puffer requires to stream media. Please try another browser or ' +
      'device.'
    );
  }

  /* used by handleVideo */
  var curr_video_quality = null;
  var curr_ssim = null;
  var curr_video_bitrate = null;  // kbps
  var partial_video_chunks = null;

  /* used by handleAudio */
  var curr_audio_quality = null;
  var partial_audio_chunks = null;

  video.src = URL.createObjectURL(ms);
  video.load();

  /* Initialize video and audio source buffers, and set the initial offset */
  function init_source_buffers() {
    console.log('Initializing new media source buffer');

    video.currentTime = init_seek_ts / timescale;

    vbuf = ms.addSourceBuffer(video_codec);
    abuf = ms.addSourceBuffer(audio_codec);

    vbuf.addEventListener('updateend', that.vbuf_update);

    vbuf.addEventListener('error', function(e) {
      console.log('video source buffer error:', e);
      that.close();
    });

    vbuf.addEventListener('abort', function(e) {
      console.log('video source buffer abort:', e);
    });

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
    if (debug && ms) {
      console.log('sourceopen: ' + ms.readyState, e);
    }

    if (ms) {  // safeguard
      init_source_buffers();
    }
  });

  ms.addEventListener('sourceended', function(e) {
    if (debug && ms) {
      console.log('sourceended: ' + ms.readyState, e);
    }
  });

  ms.addEventListener('sourceclose', function(e) {
    if (debug && ms) {
      console.log('sourceclose: ' + ms.readyState, e);
    }
    that.close();
  });

  ms.addEventListener('error', function(e) {
    if (ms) {
      console.log('media source error: ' + ms.readyState, e);
    }
    that.close();
  });

  this.isOpen = function() {
    return ms !== null && vbuf !== null && abuf !== null;
  };

  /* call "close" to garbage collect MediaSource and SourceBuffers sooner */
  this.close = function() {
    if (ms) {
      console.log('Closing media source buffer');
    }

    /* assign null to (hopefully) trigger garbage collection */
    ms = null;
    vbuf = null;
    abuf = null;

    pending_video_chunks = [];
    pending_audio_chunks = [];
  };

  this.handleVideo = function(metadata, data, msg_ts) {
    if (channel !== metadata.channel) {
      console.log('error: should have ignored data from incorrect channel');
      return;
    }

    /* send vidack */
    ws_client.send_client_ack('client-vidack', metadata);

    /* New segment or server aborted sending */
    if (curr_video_quality !== metadata.quality) {
      curr_video_quality = metadata.quality;
      partial_video_chunks = [];
    }
    partial_video_chunks.push(data);

    curr_ssim = metadata.ssim;

    /* Last fragment received */
    if (data.byteLength + metadata.byteOffset === metadata.totalByteLength) {
      /* assemble partial chunks into a complete chunk */
      pending_video_chunks.push({
        metadata: metadata,
        data: concat_arraybuffers(partial_video_chunks,
                                  metadata.totalByteLength)
      });
      partial_video_chunks = [];

      next_video_timestamp = metadata.timestamp + video_duration;
      curr_video_bitrate = 0.001 * 8 * metadata.totalByteLength /
                           (video_duration / timescale);

      /* try updating vbuf */
      that.vbuf_update();
    }
  };

  this.handleAudio = function(metadata, data, msg_ts) {
    if (channel !== metadata.channel) {
      console.log('error: should have ignored data from incorrect channel');
      return;
    }

    /* send audack */
    ws_client.send_client_ack('client-audack', metadata);

    /* New segment or server aborted sending */
    if (curr_audio_quality !== metadata.quality) {
      curr_audio_quality = metadata.quality;
      partial_audio_chunks = [];
    }
    partial_audio_chunks.push(data);

    /* Last fragment received */
    if (data.byteLength + metadata.byteOffset === metadata.totalByteLength) {
      /* assemble partial chunks into a complete chunk */
      pending_audio_chunks.push({
        metadata: metadata,
        data: concat_arraybuffers(partial_audio_chunks,
                                  metadata.totalByteLength)
      });
      partial_audio_chunks = [];

      next_audio_timestamp = metadata.timestamp + audio_duration;

      /* try updating abuf */
      that.abuf_update();
    }
  };

  /* accessors */
  this.getChannel = function() {
    return channel;
  };

  this.getVideoQuality = function() {
    return curr_video_quality;
  };

  this.getSSIM = function() {
    return curr_ssim;
  };

  this.getVideoBitrate = function() {
    return curr_video_bitrate;
  };

  this.getAudioQuality = function() {
    return curr_audio_quality;
  };

  /* Get the number of seconds of buffered video */
  this.getVideoBufferLen = function() {
    if (vbuf && vbuf.buffered.length === 1 &&
        vbuf.buffered.end(0) >= video.currentTime) {
      const ret = vbuf.buffered.end(0) - video.currentTime;
      return parseFloat(ret.toFixed(3));
    }

    return 0;
  };

  /* Set audio buffer as the min of buffered video and audio */
  this.getAudioBufferLen = function() {
    if (vbuf && vbuf.buffered.length === 1 &&
        abuf && abuf.buffered.length === 1) {
      const min_buf = Math.min(vbuf.buffered.end(0), abuf.buffered.end(0));
      if (min_buf >= video.currentTime) {
        const ret = min_buf - video.currentTime;
        return parseFloat(ret.toFixed(3));
      }
    }

    return 0;
  };

  /* If buffered *video or audio* is behind video.currentTime */
  this.isRebuffering = function() {
    if (vbuf && vbuf.buffered.length === 1 &&
        abuf && abuf.buffered.length === 1) {
      const min_buf = Math.min(vbuf.buffered.end(0), abuf.buffered.end(0));
      if (min_buf < video.currentTime) {
        return true;
      }
    }

    return false;
  };

  /* Get the expected timestamp of the next video chunk */
  this.getNextVideoTimestamp = function() {
    return next_video_timestamp;
  };

  /* Get the expected timestamp of the next audio chunk */
  this.getNextAudioTimestamp = function() {
    return next_audio_timestamp;
  };

  /* Push data onto the SourceBuffers if they are ready */
  this.vbuf_update = function() {
    if (vbuf && !vbuf.updating && pending_video_chunks.length > 0) {
      var next_video = pending_video_chunks.shift();
      vbuf.appendBuffer(next_video.data);
    }
  };

  this.abuf_update = function() {
    if (abuf && !abuf.updating && pending_audio_chunks.length > 0) {
      var next_audio = pending_audio_chunks.shift();
      abuf.appendBuffer(next_audio.data);
    }
  };
}

function WebSocketClient(session_key, username, settings_debug, sysinfo) {
  /* if DEBUG = True in settings.py, connect to non-secure WebSocket server */
  debug = settings_debug;

  var that = this;

  var ws = null;
  var av_source = null;

  /* increment init_id every time a client-init is sent */
  var init_id = 0;

  /* record the screen sizes reported to the server as they might change */
  var screen_height = null;
  var screen_width = null;

  /* exponential backoff to reconnect */
  var reconnect_backoff = BASE_RECONNECT_BACKOFF;

  var set_channel_ts = null;  /* timestamp (in ms) of setting a channel */
  var startup_delay_ms = null;

  var rebuffer_start_ts = null;  /* timestamp (in ms) of starting to rebuffer */
  var last_rebuffer_ts = null;  /* timestamp (in ms) of last rebuffer */
  var cumulative_rebuffer_ms = 0;

  var channel_error = false;

  this.send_client_init = function(channel) {
    if (fatal_error) {
      return;
    }

    if (!(ws && ws.readyState === WS_OPEN)) {
      return;
    }

    init_id += 1;

    screen_height = screen.height;
    screen_width = screen.width;

    var msg = {
      initId: init_id,
      sessionKey: session_key,
      userName: username,
      channel: channel,
      os: sysinfo.os,
      browser: sysinfo.browser,
      screenHeight: screen_height,
      screenWidth: screen_width
    };

    /* try resuming if the client is already watching the same channel */
    if (av_source && av_source.getChannel() === channel) {
      msg.nextVts = av_source.getNextVideoTimestamp();
      msg.nextAts = av_source.getNextAudioTimestamp();
    }

    ws.send(format_client_msg('client-init', msg));

    if (debug) {
      console.log('sent client-init', msg);
    }
  };

  this.send_client_info = function(info_event) {
    if (fatal_error || channel_error) {
      return;
    }

    if (!(ws && ws.readyState === WS_OPEN)) {
      return;
    }

    /* note that it is fine if av_source.isOpen() is false */
    if (!av_source) {
      return;
    }

    var msg = {
      initId: init_id,
      event: info_event,
      videoBufferLen: av_source.getVideoBufferLen(),
      audioBufferLen: av_source.getAudioBufferLen(),
      cumRebufferTime: cumulative_rebuffer_ms
    };

    /* include screen sizes if they have changed */
    if (screen.height !== screen_height || screen.width !== screen_width) {
      screen_height = screen.height;
      screen_width = screen.width;
      msg.screenHeight = screen_height;
      msg.screenWidth = screen_width;
    }

    ws.send(format_client_msg('client-info', msg));

    if (debug) {
      console.log('sent client-info', msg);
    }
  };

  /* ack_type: 'client-vidack' or 'client-audack' */
  this.send_client_ack = function(ack_type, data_to_ack) {
    if (fatal_error || channel_error) {
      return;
    }

    if (!(ws && ws.readyState === WS_OPEN)) {
      return;
    }

    /* note that it is fine if av_source.isOpen() is false */
    if (!av_source) {
      return;
    }

    var msg = {
      initId: init_id,
      videoBufferLen: av_source.getVideoBufferLen(),
      audioBufferLen: av_source.getAudioBufferLen()
    };

    msg.channel = data_to_ack.channel;
    msg.quality = data_to_ack.quality;
    msg.timestamp = data_to_ack.timestamp;

    msg.byteOffset = data_to_ack.byteOffset;
    msg.totalByteLength = data_to_ack.totalByteLength;
    /* byteLength is a new field we added to metadata */
    msg.byteLength = data_to_ack.byteLength;

    if (ack_type === 'client-vidack') {
      msg.ssim = data_to_ack.ssim;

      ws.send(format_client_msg(ack_type, msg));
    } else if (ack_type === 'client-audack') {
      ws.send(format_client_msg(ack_type, msg));
    } else {
      console.log('invalid ack type:', ack_type);
      return;
    }

    if (debug) {
      console.log('sent', ack_type, msg);
    }
  };

  /* handle a WebSocket message from the server */
  function handle_ws_msg(e) {
    const msg_ts = e.timeStamp;
    const server_msg = parse_server_msg(e.data);

    var metadata = server_msg.metadata;
    /* ignore outdated messages from the server */
    if (metadata.initId !== init_id) {
      return;
    }

    const data = server_msg.data;
    /* always add one more field to metadata: total length of data */
    metadata.byteLength = data.byteLength;

    if (debug) {
      console.log('received', metadata.type, metadata);
    }

    if (metadata.type === 'server-error') {
      if (metadata.errorType === 'drop') {
        /* server is going to drop this connection
         * now disconnect and never reconnect */
        set_fatal_error(metadata.errorMessage);
        ws.close();
      } else if (metadata.errorType === 'channel') {
        /* this channel is not currently available */
        add_player_error(metadata.errorMessage, 'channel');
        channel_error = true;
      }
    } else if (metadata.type === 'server-init') {
      /* return if client is able to resume */
      if (av_source && av_source.isOpen() && metadata.canResume) {
        console.log('Resuming playback');
        return;
      }

      /* create a new AVSource if it does not exist or unable to resume */
      av_source = new AVSource(that, metadata);
    } else if (metadata.type === 'server-video') {
      if (!av_source) {
        console.log('Error: AVSource is not initialized yet');
        return;
      }

      /* reset reconnect_backoff once a new media chunk is received */
      reconnect_backoff = BASE_RECONNECT_BACKOFF;

      /* note: handleVideo can buffer chunks even if !av_source.isOpen() */
      av_source.handleVideo(metadata, data, msg_ts);
    } else if (metadata.type === 'server-audio') {
      if (!av_source) {
        console.log('Error: AVSource is not initialized yet');
        return;
      }

      /* reset reconnect_backoff once a new media chunk is received */
      reconnect_backoff = BASE_RECONNECT_BACKOFF;

      /* note: handleAudio can buffer chunks even if !av_source.isOpen() */
      av_source.handleAudio(metadata, data, msg_ts);
    } else {
      console.log('received unknown message', metadata);
    }
  }

  this.connect = function(channel) {
    const ws_host_port = location.hostname + ':9361';
    const ws_addr = debug ? 'ws://' + ws_host_port
                          : 'wss://' + ws_host_port;
    ws = new WebSocket(ws_addr);

    ws.binaryType = 'arraybuffer';
    ws.onmessage = handle_ws_msg;

    ws.onopen = function(e) {
      console.log('Connected to', ws_addr);
      remove_player_error('connect');

      /* shouldn't call set_channel, which is reserved for channel switching */
      set_channel_helper(channel);
    };

    ws.onclose = function(e) {
      console.log('Closed connection to', ws_addr);
      ws = null;

      if (fatal_error) {
        return;
      }

      if (reconnect_backoff < MAX_RECONNECT_BACKOFF) {
        /* Try to reconnect */
        console.log('Reconnecting in ' + reconnect_backoff + 'ms');

        setTimeout(function() {
          add_player_error(
            'Error: failed to connect to server. Reconnecting...', 'connect'
          );

          if (av_source) {
            /* Try to resume the connection */
            that.connect(av_source.getChannel());
          } else {
            that.connect(channel);
          }
        }, reconnect_backoff);

        reconnect_backoff = reconnect_backoff * 2;
      } else {
        set_fatal_error(
          'Error: failed to connect to server. ' +
          'Please refresh the page or try again later.'
        );
      }
    };

    ws.onerror = function(e) {
      console.log('WebSocket error:', e);
      ws = null;
    };
  };

  function set_channel_helper(channel) {
    /* render UI */
    start_spinner();
    remove_player_error('channel');
    channel_error = false;

    /* send client-init */
    that.send_client_init(channel);

    /* reset stats */
    set_channel_ts = Date.now();
    startup_delay_ms = null;

    rebuffer_start_ts = null;
    last_rebuffer_ts = null;
    cumulative_rebuffer_ms = 0;
  }

  /* used when switching channels */
  this.set_channel = function(channel) {
    /* call 'close' to allocate a new MediaSource more quickly later */
    if (av_source) {
      av_source.close();
    }

    set_channel_helper(channel);
  };

  video.oncanplay = function() {
    var play_promise = video.play();

    if (play_promise) {
      play_promise.then(function() {
        // playback started
      }).catch(function(error) {
        // playback failed
        add_player_error(
          'Error: failed to play the video. Please try a different channel ' +
          ' or refresh the page', 'channel');
      });
    }
  };

  /* check if *video or audio* is rebuffering every 50 ms */
  function check_rebuffering() {
    if (!(av_source && av_source.isOpen())) {
      return;
    }
    const rebuffering = av_source.isRebuffering();
    const curr_ts = Date.now();

    if (startup_delay_ms === null) {
      if (!rebuffering) {
        /* this is the first time that the channel has started playing */
        stop_spinner();
        console.log('Channel starts playing');

        /* calculate startup delay */
        startup_delay_ms = curr_ts - set_channel_ts;
        cumulative_rebuffer_ms += startup_delay_ms;

        /* inform server of startup delay (via cumulative_rebuffer_ms) */
        that.send_client_info('startup');
      }

      /* always return when startup_delay_ms is null */
      return;
    }

    if (rebuffering) {
      if (rebuffer_start_ts === null) {
        /* channel stops playing and starts rebuffering */
        start_spinner();
        console.log('Channel starts rebuffering');

        /* inform server */
        that.send_client_info('rebuffer');

        /* record the starting point of rebuffering */
        rebuffer_start_ts = curr_ts;
      }

      /* update cumulative rebuffering */
      if (last_rebuffer_ts !== null) {
        cumulative_rebuffer_ms += curr_ts - last_rebuffer_ts;
      }

      /* record this rebuffering */
      last_rebuffer_ts = curr_ts;
    } else {
      if (rebuffer_start_ts !== null) {
        /* the channel resumes playing from rebuffering */
        stop_spinner();
        console.log('Channel resumes playing');

        /* inform server */
        that.send_client_info('play');

        /* record that video resumes playing */
        rebuffer_start_ts = null;
      }

      /* record that video is playing */
      last_rebuffer_ts = null;
    }
  }
  setInterval(check_rebuffering, 50);

  /* send client-info timer every 250 ms */
  const send_client_info_timer_interval = 250;
  function send_client_info_timer() {
    that.send_client_info('timer');
  }
  setInterval(send_client_info_timer, send_client_info_timer_interval);

  /* update debug info every 500 ms */
  const update_debug_info_interval = 500;
  function update_debug_info() {
    if (av_source && av_source.isOpen()) {
      const na = 'N/A';
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
  }
  setInterval(update_debug_info, update_debug_info_interval);
}
