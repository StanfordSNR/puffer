if (process.argv.length != 3) {
  console.log('Usage: node index.js <port-number>');
  process.exit(-1);
}

const port_num = Number(process.argv[2]);

const express = require('express');
const http = require('http');
const WebSocket = require('ws');
const fs = require('fs');
const path = require('path');

const app = express();
app.use(express.static(path.join(__dirname, '/static')));
const server = http.createServer(app);

const ws_server = new WebSocket.Server({server});
ws_server.on('connection', function(ws, req) {
  ws.binaryType = 'arraybuffer';

  // send init segment
  fs.readFile(path.join(__dirname, '/static/init.mp4'), function(err, data) {
    if (err) {
      console.log(err);
    } else {
      ws.send(data);
    }
  });

  // send media segments
  for (var i = 0; i <= 19; i++) {
    fs.readFile(path.join(__dirname, '/static', String(i * 180180) + '.m4s'),
      function(err, data) {
        if (err) {
          console.log(err);
        } else {
          ws.send(data);
        }
      });
  }
});

app.get('/', function(req, res) {
  res.sendFile('index.html');
});

server.listen(port_num, function() {
  console.log('Listening on %d', server.address().port);
});
