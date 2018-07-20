const WS_OPEN = 1;

const TIMER_INTERVAL = 1000;
const BASE_RECONNECT_BACKOFF = 100;
const MAX_RECONNECT_BACKOFF = 30000;

const debug = false;

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

function AVSource(video, audio, options) {
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

  /* Lists to store accepted segments that have not been added to the
   * SourceBuffers yet because they may be in the updating state */
  var pending_video_chunks = [];
  var pending_audio_chunks = [];

  var ms = new MediaSource();

  video.src = URL.createObjectURL(ms);
  audio.src = URL.createObjectURL(ms);

  video.load();
  audio.load();

  var that = this;

  /* Initialize video and audio source buffers, and set the initial offset */
  function init_source_buffers() {
    console.log('Initializing new AV source');
    video.currentTime = init_seek_ts / timescale;
    audio.currentTime = video.currentTime;

    vbuf = ms.addSourceBuffer(video_codec);
    vbuf.addEventListener('updateend', that.vbuf_update);
    vbuf.addEventListener('error', function(e) {
      console.log('vbuf error:', e);
      that.close();
    });
    vbuf.addEventListener('abort', function(e) {
      console.log('vbuf abort:', e);
      that.close();
    });

    abuf = ms.addSourceBuffer(audio_codec);
    abuf.addEventListener('updateend', that.abuf_update);
    abuf.addEventListener('error', function(e) {
      console.log('abuf error:', e);
      that.close();
    });
    abuf.addEventListener('abort', function(e) {
      console.log('abuf abort:', e);
      that.close();
    });

    /* check if there are already pending media chunks */
    if (pending_video_chunks.length > 0) {
      that.vbuf_update();
    }

    if (pending_audio_chunks.length > 0) {
      that.abuf_update();
    }

    /*
    var video_play_promise = video.play();
    if (video_play_promise) {
      video_play_promise.then(function() {
        console.log('video.play() succeeded');
      }).catch(function(error) {
        that.close();
      });
    }
    */
  }

  ms.addEventListener('sourceopen', function(e) {
    console.log('sourceopen: ' + ms.readyState, e);
    init_source_buffers();
  });
  ms.addEventListener('sourceended', function(e) {
    console.log('sourceended: ' + ms.readyState, e);
  });
  ms.addEventListener('sourceclose', function(e) {
    console.log('sourceclose: ' + ms.readyState, e);
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
    console.log('Closing AV source');
    pending_audio_chunks = [];
    pending_video_chunks = [];
    abuf = null;
    vbuf = null;
  };

  var partial_video_quality = null;
  var partial_video_chunks = null;
  this.handleVideo = function(data, metadata) {
    /* New segment or server aborted sending */
    if (partial_video_quality !== metadata.quality) {
      partial_video_quality = metadata.quality;
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

      /* update vbuf when there are pending_video_chunks */
      if (pending_video_chunks.length > 0) {
        that.vbuf_update();
      }

      partial_video_chunks = [];
      next_video_timestamp = metadata.timestamp + metadata.duration;
    } else if (debug) {
      console.log('video: not done receiving', metadata.timestamp);
    }
  };

  var partial_audio_quality = null;
  var partial_audio_chunks = null;
  this.handleAudio = function(data, metadata) {
    /* New segment or server aborted sending */
    if (partial_audio_quality !== metadata.quality) {
      partial_audio_quality = metadata.quality;
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

      /* update abuf when there are pending_audio_chunks */
      if (pending_audio_chunks.length > 0) {
        that.abuf_update();
      }

      partial_audio_chunks = [];
      next_audio_timestamp = metadata.timestamp + metadata.duration;
    } else if (debug) {
      console.log('audio: not done receiving', metadata.timestamp);
    }
  };

  /* Log debugging info to console */
  this.logBufferInfo = function() {
    if (vbuf) {
      if (vbuf.buffered.length > 1) {
        console.log('Error: vbuf.buffered.length=' + vbuf.buffered.length +
                    ', server not sending segments in order?');
      }

      for (var i = 0; i < vbuf.buffered.length; i++) {
        // There should only be one range if the server is
        // sending segments in order
        console.log('video range:',
                    vbuf.buffered.start(i), '-', vbuf.buffered.end(i));
      }
    }
    if (abuf) {
      if (abuf.buffered.length > 1) {
        console.log('Error: abuf.buffered.length=' + abuf.buffered.length +
                    ', server not sending segments in order?');
      }

      for (var i = 0; i < abuf.buffered.length; i++) {
        // Same comment as above
        console.log('audio range:',
                    abuf.buffered.start(i), '-', abuf.buffered.end(i));
      }
    }
  };

  this.getChannel = function() { return channel; }

  /* Get the number of seconds of video buffered */
  this.getVideoBufferLen = function() {
    if (vbuf && vbuf.buffered.length > 0) {
      return vbuf.buffered.end(0) - video.currentTime;
    } else {
      return -1;
    }
  };

  /* Get the number of seconds of audio buffered */
  this.getAudioBufferLen = function() {
    if (abuf && abuf.buffered.length > 0) {
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

function WebSocketClient(video, audio, session_key) {
  var ws = null;
  var av_source = null;

  /* Exponential backoff to reconnect */
  var rc_backoff = BASE_RECONNECT_BACKOFF;

  var that = this;

  function send_client_init(ws, channel) {
    if (ws && ws.readyState === WS_OPEN) {
      try {
        var msg = {
          sessionKey: session_key,
          playerWidth: video.videoWidth,
          playerHeight: video.videoHeight,
          channel: channel
        };

        if (av_source && av_source.getChannel() === channel) {
          msg.nextAudioTimestamp = av_source.getNextAudioTimestamp();
          msg.nextVideoTimestamp = av_source.getNextVideoTimestamp();
        }

        ws.send(format_client_msg('client-init', msg));
        console.log('sent client-init');
      } catch (e) {
        console.log(e);
      }
    }
  }

  function send_client_info(event) {
    if (debug && av_source && av_source.isOpen()) {
      av_source.logBufferInfo();
    }
    if (ws && ws.readyState === WS_OPEN && av_source && av_source.isOpen()) {
      try {
        ws.send(format_client_msg('client-info',
          {
            event: event,
            videoBufferLen: av_source.getVideoBufferLen(),
            audioBufferLen: av_source.getAudioBufferLen(),
            nextVideoTimestamp: av_source.getNextVideoTimestamp(),
            nextAudioTimestamp: av_source.getNextAudioTimestamp(),
            playerWidth: video.videoWidth,
            playerHeight: video.videoHeight,
            playerReadyState: video.readyState,
            initId: av_source.getInitId()
          }
        ));
      } catch (e) {
        console.log('Failed to send client info', e);
      }
    }
  }

  /* Handle a websocket message from the server */
  function handle_msg(e) {
    var message = parse_server_msg(e.data);

    if (message.metadata.type === 'server-hello') {
      console.log(message.metadata.type, message.metadata);
    } else if (message.metadata.type === 'server-init') {
      console.log(message.metadata.type, message.metadata);
      if (av_source && av_source.canResume(message.metadata)) {
        console.log('Resuming playback');
        av_source.resume(message.metadata);
      } else {
        if (av_source) {
          av_source.close();
        }
        av_source = new AVSource(video, audio, message.metadata);
      }
    } else if (message.metadata.type === 'server-audio') {
      if (debug) {
        console.log('received', message.metadata.type,
                    message.metadata.timestamp,
                    message.metadata.quality);
      }

      /* ignore chunks from wrong channels */
      if (av_source && av_source.getChannel() === message.metadata.channel) {
        av_source.handleAudio(message.data, message.metadata);
        send_client_info('audack');
      }
    } else if (message.metadata.type === 'server-video') {
      if (debug) {
        console.log('received', message.metadata.type,
                    message.metadata.timestamp,
                    message.metadata.quality);
      }

      /* ignore chunks from wrong channels */
      if (av_source && av_source.getChannel() === message.metadata.channel) {
        av_source.handleVideo(message.data, message.metadata);
        send_client_info('vidack');
      }
    } else {
      console.log('received unknown message', message.metadata.type);
    }
  }

  this.connect = function(channel) {
    const ws_host_and_port = location.hostname + ':9361';
    console.log('WS(S) at', ws_host_and_port);

    ws = new WebSocket('ws://' + ws_host_and_port);
    // ws = new WebSocket('wss://' + ws_host_and_port);

    ws.binaryType = 'arraybuffer';
    ws.onmessage = handle_msg;

    ws.onopen = function(e) {
      console.log('WebSocket open, sending client-init');
      send_client_init(ws, channel);
      rc_backoff = BASE_RECONNECT_BACKOFF;
    };

    ws.onclose = function(e) {
      console.log('WebSocket closed');
      ws = null;

      if (av_source && rc_backoff <= MAX_RECONNECT_BACKOFF) {
        /* Try to reconnect */
        console.log('Reconnecting in ' + rc_backoff + 'ms');

        setTimeout(function() {
          that.connect(av_source.getChannel());
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
    console.log('video canplay');
    send_client_info('canplay');
  };

  video.onwaiting = function() {
    console.log('video is rebuffering');
    send_client_info('rebuffer');
  };

  // Start sending status updates to the server
  function timer_helper() {
    audio.currentTime = video.currentTime;

    if (debug) {
      console.log('video.currentTime', video.currentTime,
                  'audio.currentTime', audio.currentTime);
    }

    send_client_info('timer');
    setTimeout(timer_helper, TIMER_INTERVAL);
  }
  timer_helper();
}


function setup_channel_bar(client){
  const channel_list = document.getElementById('channel-list')
                               .getElementsByClassName('li_channel');

  for (var i = 0; i < channel_list.length; i++) {
    channel_list[i].onclick = function() {
      const checked_channel_list = document.getElementById('channel-list')
                                   .getElementsByClassName('li_channel_checked');

      for (var j = 0; j < checked_channel_list.length; j++) {
        checked_channel_list[j].classList.add('li_channel');
        checked_channel_list[j].classList.remove('li_channel_checked');
      }

      this.classList.add('li_channel_checked');
      this.classList.remove('li_channel');

      var value = this.innerHTML;
      console.log('set channel:', value);
      client.set_channel(value);
    }
  }
}


function start_puffer(session_key) {
  const video = document.getElementById('tv-player');
  const audio = document.getElementById('tv-audio');

  video.addEventListener('loadeddata', function() {
    if (video.readyState >= 2) {
      console.log('Start playing video');
      video.play();
    }
  });

  const client = new WebSocketClient(video, audio, session_key);

  setup_channel_bar(client);

  client.connect("Unknown");
}
