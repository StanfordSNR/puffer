/* -*-mode:c++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */

#ifndef WSSERVER_HH
#define WSSERVER_HH

#include <map>
#include <set>
#include <functional>
#include <deque>

#include "socket.hh"
#include "nb_secure_socket.hh"
#include "poller.hh"
#include "address.hh"
#include "http_request_parser.hh"
#include "ws_message_parser.hh"

/* this implementation is not thread-safe. */
template<class SocketType>
class WSServer
{
public:
  typedef std::function<void(const uint64_t, const WSMessage &)> MessageCallback;
  typedef std::function<void(const uint64_t)> OpenCallback;
  typedef OpenCallback CloseCallback;

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
    } state { State::NotConnected };

    SocketType socket;

    /* incoming messages */
    HTTPRequest handshake_request {};
    HTTPRequestParser ws_handshake_parser {};
    WSMessageParser ws_message_parser {};

    /* outgoing messages */
    std::deque<std::string> send_buffer {};

    Connection(TCPSocket && sock, SSLContext & ssl_context);

    std::string read();
    void write();

    bool data_to_send() const { return not send_buffer.empty(); }
    unsigned int buffer_bytes() const;
  };

  SSLContext ssl_context_ {};

  TCPSocket listener_socket_;
  std::map<uint64_t, Connection> connections_ {};
  Poller poller_ {};

  MessageCallback message_callback_ {};
  OpenCallback open_callback_ {};
  CloseCallback close_callback_ {};

  std::set<uint64_t> closed_connections_ {};

public:
  WSServer(const Address & listener_addr);
  Poller::Result loop_once();

  Poller & poller() { return poller_; }

  SSLContext & ssl_context() { return ssl_context_; }

  void set_message_callback(MessageCallback func) { message_callback_ = func; }
  void set_open_callback(OpenCallback func) { open_callback_ = func; }
  void set_close_callback(CloseCallback func) { close_callback_ = func; }

  void queue_frame(const uint64_t connection_id, const WSFrame & frame);

  void close_connection(const uint64_t connection_id);
  unsigned int queue_bytes(const uint64_t connection_id) const;
};

using WebSocketServer = WSServer<TCPSocket>;
using WebSocketSecureServer = WSServer<NBSecureSocket>;

#endif /* WSSERVER_HH */
