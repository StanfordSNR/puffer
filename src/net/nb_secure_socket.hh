/* -*-mode:c++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */

#ifndef CONNECTION_HH
#define CONNECTION_HH

#include <string>
#include <queue>

#include "secure_socket.hh"

class NBSecureSocket
{
private:
  enum class State {needs_connect,
                    needs_ssl_read_to_connect,
                    needs_ssl_write_to_connect,
                    needs_ssl_write_to_write,
                    needs_ssl_write_to_read,
                    needs_ssl_read_to_write,
                    needs_ssl_read_to_read,
                    ready,
                    closed};

  SecureSocket socket_;
  State state_ {State::needs_connect};

  std::queue<std::string> write_buffer_ {};
  std::string read_buffer_ {};

  bool ready() const { return state_ == State::ready; }

  bool connected() const
  {
    return (state_ != State::needs_connect) and
           (state_ != State::needs_ssl_read_to_connect) and
           (state_ != State::needs_ssl_write_to_connect);
  }

  void continue_SSL_connect();
  void continue_SSL_write();
  void continue_SSL_read();

public:
  NBSecureSocket(SecureSocket && sock)
    : socket_(std::move(sock))
  {}

  bool readable() const;
  bool writable() const;

  std::string read();
  void write(const std::string & message);
};

#endif /* CONNECTION_HH */
