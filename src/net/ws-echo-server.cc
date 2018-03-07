/* -*-mode:c++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */

#include <iostream>

#include "exception.hh"
#include "ws_server.hh"

using namespace std;

int main()
{
  try {
    string ip = "0.0.0.0";
    uint16_t port = 9333;

    WSServer ws_server {{ip,port}};
    ws_server.set_message_callback(
      [](const uint64_t connection_id, const WSMessage & message)
      {
        cerr << "Message (from=" << connection_id << "): "
             << message.payload() << endl;
      }
    );

    ws_server.set_open_callback(
      [](const uint64_t connection_id)
      {
        cerr << "Connected (id=" << connection_id << ")." << endl;
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
