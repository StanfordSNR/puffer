const ws = new WebSocket('ws://localhost:8080');

ws.onmessage = function(event) {
  console.log(event.data);
};

ws.onopen = function(event) {
  ws.send('are you a server?');
};
