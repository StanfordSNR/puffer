/* -*-mode:c++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */

#include <iostream>

#include "exception.hh"
#include "ws_server.hh"

using namespace std;

void usage( char * argv0 )
{
  cerr << argv0 << " PRIVATE-KEY CERT" << endl;
}

int main( int argc, char * argv[] )
{
  if (argc == 0) {
    abort();
  }

  if (argc != 3) {
    usage( argv[0] );
    return EXIT_FAILURE;
  }

  try {
    string ip = "0.0.0.0";
    uint16_t port = 9333;

    WebSocketSecureServer ws_server {{ip,port}};
    ws_server.ssl_context().use_private_key_file(argv[1]);
    ws_server.ssl_context().use_certificate_file(argv[2]);

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
        cerr << "Connected (id=" << connection_id << ")." << endl;
      }
    );

    ws_server.set_close_callback(
      [](const uint64_t connection_id)
      {
        cerr << "Connection closed (id=" << connection_id << ")" << endl;
      }
    );

    while(ws_server.loop_once().result == Poller::Result::Type::Success);
  }
  catch (const exception & ex) {
    print_exception("ws-echo-server", ex);
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
