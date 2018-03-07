/* -*-mode:c++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */

#include "ws_server.hh"

#include <iostream>

using namespace std;
using namespace PollerShortNames;

WSServer::WSServer(const Address & listener_addr)
  : listener_socket_()
{
  listener_socket_.bind(listener_addr);
  listener_socket_.listen();

  poller_.add_action(Poller::Action(listener_socket_, Direction::In,
    [this] () -> ResultType
    {
      /* incoming connection. let's create a connection object */
      connections_.emplace_back(last_connection_id_++, listener_socket_.accept());
      Connection & connection = connections_.back();

      /* add the actions for this connection */
      poller_.add_action(Poller::Action(connection.socket, Direction::In,
        [&connection] () -> ResultType
        {
          cerr << "data in: " << connection.socket.read() << endl;
          return ResultType::Continue;
        }
      ));

      poller_.add_action(Poller::Action(connection.socket, Direction::Out,
        [&connection] () -> ResultType
        {
          connection.socket.write( "hello world!\n" );
          return ResultType::Continue;
        }
      ));

      return ResultType::Continue;
    }
  ));
}

void WSServer::serve_forever()
{
  while (poller_.poll( -1 ).result == Poller::Result::Type::Success);
}
