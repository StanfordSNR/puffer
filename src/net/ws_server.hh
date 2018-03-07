/* -*-mode:c++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */

#ifndef WSSERVER_HH
#define WSSERVER_HH

#include <vector>
#include <functional>

#include "socket.hh"
#include "poller.hh"
#include "address.hh"
#include "http_request_parser.hh"
#include "ws_message_parser.hh"

/* this implementation is not thread-safe. */
class WSServer
{
public:
  typedef std::function<void(const uint64_t connection_id,
                             const WSMessage &)> MessageHandlerFunction;

private:
  uint64_t last_connection_id_ {0};

  struct Connection
  {
    enum class State {
      NotConnected = 0,
      Connecting,
      Connected,
      Closing,
      Closed
    } state;

    uint64_t id;

    TCPSocket socket;

    HTTPRequest handshake_request {};
    HTTPRequestParser ws_handshake_parser {};
    WSMessageParser ws_message_parser {};

    Connection(const uint64_t id, TCPSocket && sock)
      : state(State::NotConnected), id(id), socket(std::move(sock)) {}
  };

  TCPSocket listener_socket_;
  std::vector<Connection> connections_ {};
  Poller poller_ {};
  MessageHandlerFunction message_handler_ {};

public:
  WSServer(const Address & listener_addr);
  void serve_forever();

  void set_message_handler(MessageHandlerFunction func) { message_handler_ = func; }
};

#endif /* WSSERVER_HH */
