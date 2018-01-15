if (process.argv.length != 3) {
  console.log('Usage: node index.js <port-number>');
  process.exit(-1);
}

const port_num = Number(process.argv[2]);

const express = require('express');
const http = require('http');
const WebSocket = require('ws');

const app = express();
app.use(express.static(__dirname + '/static'));
const server = http.createServer(app);

const wss = new WebSocket.Server({server});
wss.on('connection', function(ws, req) {
  ws.on('message', function(message) {
    console.log('Received: %s', message);
  });

  ws.send('Hello from server');
});

app.get('/', function(req, res) {
  res.sendFile('index.html');
});

server.listen(port_num, function() {
  console.log('Listening on %d', server.address().port);
});
