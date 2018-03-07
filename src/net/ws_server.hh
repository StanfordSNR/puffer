/* -*-mode:c++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */

#ifndef WSSERVER_HH
#define WSSERVER_HH

#include "socket.hh"
#include "poller.hh"
#include "address.hh"

/* this implementation is not thread-safe. */ 
class WSServer
{
private:
  uint64_t last_connection_id_ {0};

  struct Connection
  {
    uint64_t id;
    TCPSocket socket;

    Connection(const uint64_t id, TCPSocket && sock)
      : id(id), socket(std::move(sock)) {}
  };

  TCPSocket listener_socket_;
  std::vector<Connection> connections_ {};
  Poller poller_ {};

public:
  WSServer(const Address & listener_addr);
  void serve_forever();
};

#endif /* WSSERVER_HH */
