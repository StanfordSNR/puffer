'use strict';

const WS_OPEN = 1;

const TIMER_INTERVAL = 250;
const DEBUG_TIMER_INTERVAL = 500;
const BASE_RECONNECT_BACKOFF = 1000;
const MAX_RECONNECT_BACKOFF = 15000;

var debug = false;

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

function AVSource(ws_client, video, server_init) {
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

  /* last segment of chunk to ack that will be sent after updateend is fired
   * the ack can contain the up-to-date playback buffer levels in this way */
  var pending_video_to_ack = null;
  var pending_audio_to_ack = null;

  var ms = new MediaSource();

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

    vbuf.addEventListener('updateend', function(e) {
      if (pending_video_to_ack) {
        ws_client.send_client_ack('client-vidack', pending_video_to_ack);
        pending_video_to_ack = null;
      }

      that.vbuf_update();
    });

    vbuf.addEventListener('error', function(e) {
      console.log('video source buffer error:', e);
      that.close();
    });

    vbuf.addEventListener('abort', function(e) {
      console.log('video source buffer abort:', e);
    });

    abuf.addEventListener('updateend', function(e) {
      if (pending_audio_to_ack) {
        ws_client.send_client_ack('client-audack', pending_audio_to_ack);
        pending_audio_to_ack = null;
      }

      that.abuf_update();
    });

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

  this.isOpen = function() {
    return vbuf !== null && abuf !== null;
  };

  /* Close the AV source, presumably it is being replaced */
  this.close = function() {
    if (vbuf || abuf) {
      console.log('Closing media source buffer');
    }

    pending_video_chunks = [];
    pending_audio_chunks = [];

    pending_video_to_ack = null;
    pending_audio_to_ack = null;

    vbuf = null;
    abuf = null;
  };

  this.handleVideo = function(metadata, data, msg_ts) {
    if (channel !== metadata.channel) {
      console.log('error: should have ignored data from incorrect channel');
      return;
    }

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
    } else {
      /* ack intermediate fragments immediately */
      ws_client.send_client_ack('client-vidack', metadata);
    }
  };

  this.handleAudio = function(metadata, data, msg_ts) {
    if (channel !== metadata.channel) {
      console.log('error: should have ignored data from incorrect channel');
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
    } else {
      /* ack intermediate fragments immediately */
      ws_client.send_client_ack('client-audack', metadata);
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
    if (vbuf && vbuf.buffered.length == 1 &&
        vbuf.buffered.end(0) >= video.currentTime) {
      return vbuf.buffered.end(0) - video.currentTime;
    } else {
      return -1;
    }
  };

  /* Get the number of seconds of buffered audio */
  this.getAudioBufferLen = function() {
    if (abuf && abuf.buffered.length == 1 &&
        abuf.buffered.end(0) >= video.currentTime) {
      return abuf.buffered.end(0) - video.currentTime;
    } else {
      return -1;
    }
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

      if (!pending_video_to_ack) {
        pending_video_to_ack = next_video.metadata;  // sent ack in updateend handler
      } else {
        console.log("Error: called vbuf.appendBuffer() again before updateend");
      }
    }
  };

  this.abuf_update = function() {
    if (abuf && !abuf.updating && pending_audio_chunks.length > 0) {
      var next_audio = pending_audio_chunks.shift();
      abuf.appendBuffer(next_audio.data);

      if (!pending_audio_to_ack) {
        pending_audio_to_ack = next_audio.metadata;  // sent ack in updateend handler
      } else {
        console.log("Error: called abuf.appendBuffer() again before updateend");
      }
    }
  };
}

function WebSocketClient(session_key, username, sysinfo) {
  var that = this;
  var video = document.getElementById('tv-video');

  var ws = null;
  var av_source = null;

  /* increment every time a client-init is sent */
  var init_id = 0;

  /* record the screen sizes reported to the server as they might change */
  var screen_height = null;
  var screen_width = null;

  /* exponential backoff to reconnect */
  var reconnect_backoff = BASE_RECONNECT_BACKOFF;
  var last_ws_open = null;

  this.send_client_init = function(channel) {
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
    } else {
      /* cannot resume */
      if (av_source) {
        av_source.close();
      }
    }

    ws.send(format_client_msg('client-init', msg));

    if (debug) {
      console.log('sent client-init', msg);
    }
  };

  this.send_client_info = function(info_event) {
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
      audioBufferLen: av_source.getAudioBufferLen()
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

    if (ack_type == 'client-vidack') {
      msg.ssim = data_to_ack.ssim;

      ws.send(format_client_msg(ack_type, msg));
    } else if (ack_type == 'client-audack') {
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
    const data = server_msg.data;
    /* always add one more field to metadata: total length of data */
    metadata.byteLength = data.byteLength;

    /* ignore outdated messages from the server */
    if (metadata.initId !== init_id) {
      return;
    }

    if (debug) {
      console.log('received', metadata.type, metadata);
    }

    if (metadata.type === 'server-init') {
      /* return if client is able to resume */
      if (av_source && av_source.isOpen() && metadata.canResume) {
        console.log('Resuming playback');
        return;
      }

      /* create a new AVSource if it does not exist or unable to resume */
      if (av_source) {
        av_source.close();
      }
      av_source = new AVSource(that, video, metadata);
    } else if (metadata.type === 'server-video') {
      if (!av_source) {
        console.log('Error: AVSource is not initialized yet');
        return;
      }

      /* note: handleVideo can buffer chunks even if !av_source.isOpen() */
      av_source.handleVideo(metadata, data, msg_ts);
    } else if (metadata.type === 'server-audio') {
      if (!av_source) {
        console.log('Error: AVSource is not initialized yet');
        return;
      }

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
      last_ws_open = Date.now();

      that.send_client_init(channel);
    };

    ws.onclose = function(e) {
      console.log('Closed connection to', ws_addr);
      ws = null;

      /* reset reconnect_backoff if WebSocket has been open for a while */
      if (last_ws_open && Date.now() - last_ws_open > MAX_RECONNECT_BACKOFF) {
        reconnect_backoff = BASE_RECONNECT_BACKOFF;
      }

      last_ws_open = null;

      if (reconnect_backoff <= MAX_RECONNECT_BACKOFF) {
        /* Try to reconnect */
        console.log('Reconnecting in ' + reconnect_backoff + 'ms');

        setTimeout(function() {
          if (av_source) {
            /* Try to resume the connection */
            that.connect(av_source.getChannel());
          } else {
            that.connect(channel);
          }
        }, reconnect_backoff);

        reconnect_backoff = reconnect_backoff * 2;
      }
    };

    ws.onerror = function(e) {
      console.log('WebSocket error:', e);
      ws = null;
    };
  };

  this.set_channel = function(channel) {
    that.send_client_init(channel);
  };

  video.oncanplay = function() {
    console.log('Video can play');
    that.send_client_info('canplay');
  };

  video.onwaiting = function() {
    console.log('Video is rebuffering');
    that.send_client_info('rebuffer');
  };

  /* send status updates to the server from time to time */
  function timer_helper() {
    that.send_client_info('timer');
    setTimeout(timer_helper, TIMER_INTERVAL);
  }
  timer_helper();

  /* display debugging information on the client side */
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

function start_puffer(session_key, username, sysinfo, settings_debug) {
  /* if DEBUG = True in settings.py, connect to non-secure WebSocket server */
  debug = settings_debug;

  return new WebSocketClient(session_key, username, sysinfo);
}
