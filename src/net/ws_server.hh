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
  using MessageCallback = std::function<void(const uint64_t, const WSMessage &)>;
  using OpenCallback = std::function<void(const uint64_t)>;
  using CloseCallback = std::function<void(const uint64_t)>;

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
    HTTPRequestParser ws_handshake_parser {};
    WSMessageParser ws_message_parser {};

    /* outgoing messages */
    std::deque<std::string> send_buffer {};
    size_t send_buffer_offset {0};

    Connection(TCPSocket && sock, SSLContext & ssl_context);

    std::string read();
    void write();

    /* the connection has data to write to TCPSocket directly,
     * or write to NBSecureSocket's internal send_buffer */
    bool data_to_write() const { return send_buffer.size() > 0; }

    /* tell the poller if the connection is interested in sending
     * i.e., it or its NBSecureSocket has pending data in the send_buffer */
    bool interested_in_sending() const;

    unsigned int buffer_bytes() const;
    void clear_buffer();
  };

  SSLContext ssl_context_ {};

  TCPSocket listener_socket_ {};
  Address listener_addr_ {};
  std::map<uint64_t, Connection> connections_ {};
  Poller poller_ {};

  MessageCallback message_callback_ {};
  OpenCallback open_callback_ {};
  CloseCallback close_callback_ {};

  std::set<uint64_t> closed_connections_ {};

  std::string congestion_control_ {};

  void init_listener_socket();

  /* gracefully close the connection */
  void wait_close_connection(const uint64_t connection_id);

  /* force close the connection */
  void force_close_connection(const uint64_t connection_id);

public:
  WSServer(const Address & listener_addr,
           const std::string & congestion_control = "default");

  Poller::Result loop_once();
  int loop();

  Poller & poller() { return poller_; }

  SSLContext & ssl_context() { return ssl_context_; }

  void set_message_callback(MessageCallback func) { message_callback_ = func; }
  void set_open_callback(OpenCallback func) { open_callback_ = func; }
  void set_close_callback(CloseCallback func) { close_callback_ = func; }

  bool queue_frame(const uint64_t connection_id, const WSFrame & frame);

  Address peer_addr(const uint64_t connection_id) const;

  unsigned int buffer_bytes(const uint64_t connection_id) const;
  void clear_buffer(const uint64_t connection_id);

  /* public method to gracefully close a connection */
  void close_connection(const uint64_t connection_id);

  /* force close an idle connection and no longer poll on its socket */
  void clean_idle_connection(const uint64_t connection_id);

  TCPInfo get_tcp_info(const uint64_t connection_id) const;
};

using WebSocketTCPServer = WSServer<TCPSocket>;
using WebSocketSecureServer = WSServer<NBSecureSocket>;

#endif /* WSSERVER_HH */
