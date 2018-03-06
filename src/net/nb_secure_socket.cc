/* -*-mode:c++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */

#include "nb_secure_socket.hh"

#include <cassert>

using namespace std;

void NBSecureSocket::continue_SSL_connect()
{
  if (state_ == State::needs_connect) {
    verify_no_errors();
    /* TCP successfully connected, so start SSL session */
    state_ = State::needs_ssl_write_to_connect;
  }

  if (state_ == State::needs_ssl_write_to_connect or
      state_ == State::needs_ssl_read_to_connect) {
    try {
      connect();
    }
    catch (const ssl_error & s) {
      /* is it a WANT_READ or WANT_WRITE? */
      switch (s.error_code()) {
      case SSL_ERROR_WANT_READ:
        state_ = State::needs_ssl_read_to_connect;
        break;

      case SSL_ERROR_WANT_WRITE:
        state_ = State::needs_ssl_write_to_connect;
        break;

      default:
        throw;
      }

      return;
    }

    state_ = State::ready;
    return;
  }

  assert(ready());
  throw runtime_error("session already connected");
}

void NBSecureSocket::continue_SSL_write()
{
  if (not something_to_write()) {
    return;
  }

  try {
    SecureSocket::write(write_buffer_.front(), state_ == State::needs_ssl_read_to_write);
  }
  catch (ssl_error & s) {
    switch (s.error_code()) {
    case SSL_ERROR_WANT_READ:
      state_ = State::needs_ssl_read_to_write;
      break;

    case SSL_ERROR_WANT_WRITE:
      state_ = State::needs_ssl_write_to_write;
      break;

    default:
      throw;
    }

    return;
  }

  write_buffer_.pop();
  state_ = State::ready;
}

void NBSecureSocket::continue_SSL_read()
{
  try {
    read_buffer_ += SecureSocket::read(state_ == State::needs_ssl_write_to_read);
  }
  catch (ssl_error & s) {
    switch (s.error_code()) {
    case SSL_ERROR_WANT_READ:
      state_ = State::needs_ssl_read_to_read;
      break;

    case SSL_ERROR_WANT_WRITE:
      state_ = State::needs_ssl_write_to_read;
      break;

    default:
      throw;
    }

    return;
  }

  state_ = State::ready;
}

string NBSecureSocket::ezread()
{
  string buffer {move(read_buffer_)};
  read_buffer_ = string {};
  return buffer;
}
