/* -*-mode:c++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */

#ifndef WSSERVER_HH
#define WSSERVER_HH

#include <map>
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
  typedef std::function<void(const uint64_t, const WSMessage &)> MessageCallback;
  typedef std::function<void(const uint64_t)> OpenCallback;

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

    TCPSocket socket;

    HTTPRequest handshake_request {};
    HTTPRequestParser ws_handshake_parser {};
    WSMessageParser ws_message_parser {};

    Connection(TCPSocket && sock)
      : state(State::NotConnected), socket(std::move(sock)) {}
  };

  TCPSocket listener_socket_;
  std::map<uint64_t, Connection> connections_ {};
  Poller poller_ {};

  MessageCallback message_callback_ {};
  OpenCallback open_callback_ {};

public:
  WSServer(const Address & listener_addr);
  Poller::Result loop_once();

  void set_message_callback(MessageCallback func) { message_callback_ = func; }
  void set_open_callback(OpenCallback func) { open_callback_ = func; }
};

#endif /* WSSERVER_HH */
