#include <iostream>

#include "ws_server.hh"

using namespace std;

void print_usage(const string & program_name)
{
  cerr << program_name << " <YAML configuration>" << endl;
}

int main(int argc, char * argv[])
{
  if (argc < 1) {
    abort();
  }

  if (argc != 2) {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  string ip = "0.0.0.0";
  uint16_t port = 8080;

  WebSocketServer ws_server {{ip, port}};

  ws_server.set_message_callback(
    [&ws_server](const uint64_t connection_id, const WSMessage & message)
    {
      cerr << "Message (from=" << connection_id << "): "
           << message.payload() << endl;

      WSFrame echo_frame {true, message.type(), message.payload()};
      ws_server.queue_frame(connection_id, echo_frame);
    }
  );

  ws_server.set_open_callback(
    [](const uint64_t connection_id)
    {
      cerr << "Connected (id=" << connection_id << ")" << endl;
    }
  );

  ws_server.set_close_callback(
    [](const uint64_t connection_id)
    {
      cerr << "Connection closed (id=" << connection_id << ")" << endl;
    }
  );

  while (ws_server.loop_once().result == Poller::Result::Type::Success);

  return EXIT_SUCCESS;
}
