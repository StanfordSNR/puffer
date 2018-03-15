const WS_OPEN = 1;

const SEND_INFO_INTERVAL = 1000; // 1s

/* If the video offset causes the start of the first chunk
 * to go negative, the first video segment may get dropped,
 * causing the video to not play.
 * This ensures that videoOffset - adjustment > 0 */
const VIDEO_OFFSET_ADJUSTMENT = 0.05;

const DEBUG = false;

const HTML_MEDIA_READY_STATES = [
  'HAVE_NOTHING', 'HAVE_METADATA', 'HAVE_CURRENT_DATA', 'HAVE_FUTURE_DATA',
  'HAVE_ENOUGH_DATA'
];

/* Server messages are of the form: "short_metadata_len|metadata_json|data" */
function parse_server_msg(data) {
  var header_len = new DataView(data, 0, 2).getUint16();
  return {
    metadata: JSON.parse(new TextDecoder().decode(
        data.slice(2, 2 + header_len))),
    data: data.slice(2 + header_len)
  };
};

/* Client messages are of the form: "message_type json_data" */
function format_client_msg(msg_type, data) {
  return msg_type + ' ' + JSON.stringify(data);
};

/* Concatenates an array of arraybuffers */
function concat_arraybuffers(arr) {
  var len = arr.reduce((acc, x) => acc + x.length);
  var tmp = new Uint8Array(len);
  arr.reduce(function (i, x) {
    tmp.set(new Uint8Array(x), i);
    return i + x.length;
  })
  return tmp.buffer;
};

function AVSource(video, audio, options) {
  /* SourceBuffers for audio and video */
  var vbuf, abuf;

  var channel = options.channel;
  var video_codec = options.videoCodec;
  var audio_codec = options.audioCodec;
  var timescale = options.timescale;
  var init_timestamp = options.initTimestamp;

  /* Timestamps for the next chunks that the player is expecting */
  var next_audio_timestamp = init_timestamp;
  var next_video_timestamp = init_timestamp;

  /* Lists to store accepted segments that have not been added to the 
   * SourceBuffers yet because they may be in the updating state */
  var pending_video_chunks = [];
  var pending_audio_chunks = [];

  var ms = new MediaSource();
  video.src = window.URL.createObjectURL(ms);
  audio.src = window.URL.createObjectURL(ms);

  var that = this;

  /* Initializes the video and audio source buffers, and sets the initial
   * offset */
  function init_source_buffers() {
    var time_offset = init_timestamp / timescale + VIDEO_OFFSET_ADJUSTMENT;

    vbuf = ms.addSourceBuffer(video_codec);
    vbuf.timestampOffset = time_offset;
    vbuf.addEventListener('updateend', that.update);
    vbuf.addEventListener('error', function(e) {
      console.log('vbuf error:', e);
      that.close();
    });
    vbuf.addEventListener('abort', function(e) {
      console.log('vbuf abort:', e);
      that.close();
    });

    abuf = ms.addSourceBuffer(audio_codec);
    abuf.timestampOffset = time_offset;
    abuf.addEventListener('updateend', that.update);
    abuf.addEventListener('error', function(e) {
      console.log('abuf error:', e);
      that.close();
    });
    abuf.addEventListener('abort', function(e) {
      console.log('abuf abort:', e);
      that.close();
    });
  };

  ms.addEventListener('sourceopen', function(e) {
    console.log('sourceopen: ' + ms.readyState);
    init_source_buffers();
  });
  ms.addEventListener('sourceended', function(e) {
    console.log('sourceended: ' + ms.readyState);
  });
  ms.addEventListener('sourceclose', function(e) {
    console.log('sourceclose: ' + ms.readyState);
    that.close();
  });
  ms.addEventListener('error', function(e) {
    console.log('media source error: ' + ms.readyState);
    that.close();
  });

  this.isOpen = function() { return abuf != undefined && vbuf != undefined; };

  /* Close the AV source, presumably it is being replaced */
  this.close = function() {
    console.log('Closing AV source');
    pending_audio_chunks = [];
    pending_video_chunks = [];
    abuf = undefined;
    vbuf = undefined;
  };

  var partial_video_qualiity = undefined;
  var partial_video_chunks;
  this.handleVideo = function(data, metadata) {
    /* New segment or server aborted sending */
    if (partial_video_qualiity != metadata.quality) {
      partial_video_qualiity = metadata.quality;
      partial_video_chunks = [];
    }
    partial_video_chunks.push(data);

    /* Last fragment received */
    if (data.length + metadata.byteBffset == metadata.totalByteLength) {
      pending_video_chunks.push(concat_arraybuffers(partial_video_chunks));
      partial_video_chunks = undefined;
      next_video_timestamp = metadata.timestamp + metadata.duration;
    }
  };

  var partial_audio_quality = undefined;
  var partial_audio_chunks;
  this.handleAudio = function(data, metadata) {
    /* New segment or server aborted sending */
    if (partial_audio_quality != metadata.quality) {
      partial_audio_quality = metadata.quality;
      partial_audio_chunks = [];
    }
    partial_audio_chunks.push(data);

    /* Last fragment received */
    if (data.length + metadata.byteBffset == metadata.totalByteLength) {
      pending_audio_chunks.push(concat_arraybuffers(partial_audio_chunks));
      partial_audio_chunks = undefined;
      next_audio_timestamp = metadata.timestamp + metadata.duration;
    }
  };

  /* Log debugging info to console */
  this.logBufferInfo = function() {
    if (vbuf) {
      for (var i = 0; i < vbuf.buffered.length; i++) {
        // There should only be one range if the server is
        // sending segments in order
        console.log('video range:',
                    vbuf.buffered.start(i), '-', vbuf.buffered.end(i));
      }
    }
    if (abuf) {
      for (var i = 0; i < abuf.buffered.length; i++) {
        // Same comment as above
        console.log('audio range:',
                    abuf.buffered.start(i), '-', abuf.buffered.end(i));
      }
    }
  };

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

  /* Pushes data onto the SourceBuffers if they are ready */
  this.update = function() {
    if (vbuf && !vbuf.updating
      && pending_video_chunks.length > 0) {
      vbuf.appendBuffer(pending_video_chunks.shift());
    }
    if (abuf && !abuf.updating
      && pending_audio_chunks.length > 0) {
      abuf.appendBuffer(pending_audio_chunks.shift());
    }
  };
};

function WebSocketClient(video, audio, channel_select) {
  var ws;
  var av_source;
  var init_time = new Date();

  var that = this;

  /* Updates the list to show the available channels */
  function update_channel_select(channels) {
    for (var i = 0; i < channels.length; i++) {
      var option = document.createElement('option');
      option.value = channels[i];
      option.text = channels[i].toUpperCase();
      channel_select.appendChild(option);
    }
  };

  /* Handle a websocket message from the server */
  function handle_msg(e) {
    var message = parse_server_msg(e.data);
    if (message.metadata.type == 'server-hello') {
      console.log(message.metadata.type, message.metadata.channels);
      update_channel_select(message.metadata.channels);
      that.set_channel(message.metadata.channels[0]); // may be redundant

    } else if (message.metadata.type == 'server-init') {
      console.log(message.metadata.type);
      if (av_source) {
        av_source.close(); // Close any existing source
      }
      av_source = new AVSource(video, audio, message.metadata);

    } else if (message.metadata.type == 'audio') {
      console.log('received', message.metadata.type, message.metadata.quality);
      av_source.handleAudio(message.data, message.metadata);

    } else if (message.metadata.type == 'video') {
      console.log('received', message.metadata.type, message.metadata.quality);
      av_source.handleVideo(message.data, message.metadata);
    }

    if (av_source) {
      av_source.update();
    }
  };

  function send_client_init(ws, channel) {
    if (ws && ws.readyState == WS_OPEN) {
      try {
        ws.send(format_client_msg('client-init', 
          {
            channel: channel,
            playerWidth: video.videoWidth,
            playerHeight: video.videoHeight
          }
        ));
      } catch (e) {
        console.log(e);
      }
    }
  };

  function send_client_info(event) {
    if (DEBUG && av_source && av_source.isOpen()) {
      av_source.logBufferInfo();
    }
    if (ws && ws.readyState == WS_OPEN && av_source && av_source.isOpen()) {
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
          }
        ));
      } catch (e) {
        console.log('Failed to send client info', e);
      }
    }
  };

  this.connect = function() {
    ws = new WebSocket('ws://' + location.host);
    ws.binaryType = 'arraybuffer';
    ws.onmessage = handle_msg;

    ws.onopen = function (e) {
      console.log('WebSocket open, sending client-hello');
      send_client_init(ws, '');
    };

    ws.onclose = function (e) {
      console.log('WebSocket closed');
      if (av_source && av_source.isOpen()) {
        av_source.close();
      }
      alert('WebSocket closed. Refresh the page to reconnect.');
    };

    ws.onerror = function (e) {
      console.log('WebSocket error:', e);
    };
  };

  this.set_channel = function(channel) {
    send_client_init(channel);
  };

  video.oncanplay = function() {
    console.log('canplay');
    send_client_info('canplay');
  };

  video.onwaiting = function() {
    console.log('rebuffer');
    send_client_info('rebuffer');
  };

  // Start sending status updates to the server
  function timer_helper() {
    send_client_info('timer');
    setTimeout(timer_helper, SEND_INFO_INTERVAL);
  }
  timer_helper();
}

window.onload = function() {
  const video = document.getElementById('tv-player');
  const audio = document.getElementById('tv-audio');

  const mute_button = document.getElementById('mute-button');
  const full_screen_button = document.getElementById('full-screen-button');
  const volume_bar = document.getElementById('volume-bar');
  const channel_select = document.getElementById('channel-select');

  const client = new WebSocketClient(video, audio, channel_select);

  mute_button.onclick = function() {
    video.volume = 0;
    volume_bar.value = 0;
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

  volume_bar.value = video.volume;
  volume_bar.onchange = function() {
    video.volume = volume_bar.value;
  };

  channel_select.onchange = function() {
    console.log('set channel:', channel_select.value);
    client.set_channel(channel_select.value);
  };

  client.connect();
};
