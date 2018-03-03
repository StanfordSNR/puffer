/* -*-mode:c++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */

#ifndef CONNECTION_HH
#define CONNECTION_HH

#include <string>

#include "socket.hh"
#include "secure_socket.hh"

class Connection
{
public:
  enum class State {needs_connect, ready, closed};

private:
  State state_ {State::needs_connect};
  TCPSocket socket_;
  std::string write_buffer_ {};

  bool something_to_write_ {true};

public:
  Connection(TCPSocket && sock)
    : socket_(std::move(sock))
  {}

  void write( const std::string & data );
  std::string read();

  State state() const { return state_; }
  bool ready() const { return state_ == State::ready; }
};

/* struct SSLConnection
{
  enum class State {needs_connect,
                    needs_ssl_read_to_connect,
                    needs_ssl_write_to_connect,
                    needs_ssl_write_to_write,
                    needs_ssl_write_to_read,
                    needs_ssl_read_to_write,
                    needs_ssl_read_to_read,
                    ready,
                    closed};

  State state {State::needs_connect};
  SecureSocket socket;
  bool something_to_write {true};

  SSLConnectionContext(SecureSocket && sock)
    : socket(std::move(sock))
  {}

  bool ready() const { return state == State::ready; }

  bool connected() const
  {
    return (state != State::needs_connect) and
           (state != State::needs_ssl_read_to_connect) and
           (state != State::needs_ssl_write_to_connect);
  }

  void continue_SSL_connect();
  void continue_SSL_write();
  void continue_SSL_read();
}; */

#endif /* CONNECTION_HH */
