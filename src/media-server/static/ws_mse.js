// create websocket
const ws = new WebSocket('ws://localhost:8080');
ws.binaryType = 'arraybuffer';

var buffer;

// media source
const ms = new MediaSource();

const video = document.getElementById('tv-player');
video.src = window.URL.createObjectURL(ms);

ms.addEventListener('sourceopen', function(e) {
  console.log('sourceopen: ' + ms.readyState);

  buffer = ms.addSourceBuffer('video/mp4; codecs="avc1.42E020"');

  ws.onmessage = function(e) {
    buffer.appendBuffer(new Uint8Array(e.data));
  };

  video.play();

  // source buffer event listeners for debugging
  buffer.addEventListener('updatestart', function(e) {
    console.log('updatestart: ' + ms.readyState);
  });

  buffer.addEventListener('updateend', function(e) {
    console.log('updateend: ' + ms.readyState);
  });

  buffer.addEventListener('error', function(e) {
    console.log('source buffer error: ' + ms.readyState);
    ws.close();
  });

  buffer.addEventListener('abort', function(e) {
    console.log('abort: ' + ms.readyState);
  });

  buffer.addEventListener('update', function(e) {
    console.log('update: ' + ms.readyState);
  });
});

// other media source event listeners for debugging
ms.addEventListener('sourceended', function(e) {
  console.log('sourceended: ' + ms.readyState);
});

ms.addEventListener('sourceclose', function(e) {
  console.log('sourceclose: ' + ms.readyState);
});

ms.addEventListener('error', function(e) {
  console.log('media source error: ' + ms.readyState);
});
