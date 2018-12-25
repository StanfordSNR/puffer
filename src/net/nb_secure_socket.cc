/* -*-mode:c++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */

#include "nb_secure_socket.hh"

#include <cassert>

using namespace std;

void NBSecureSocket::connect()
{
  mode_ = Mode::connect;
  state_ = State::needs_connect;
}

void NBSecureSocket::accept()
{
  mode_ = Mode::accept;
  state_ = State::needs_accept;
}

void NBSecureSocket::continue_SSL_connect()
{
  if (state_ == State::needs_connect) {
    verify_no_errors();
  }

  if (state_ == State::needs_connect or
      state_ == State::needs_ssl_write_to_connect or
      state_ == State::needs_ssl_read_to_connect)
  {
    try {
      SecureSocket::connect();
    }
    catch (const ssl_error & s) {
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

void NBSecureSocket::continue_SSL_accept()
{
  if (state_ == State::needs_accept) {
    verify_no_errors();
  }

  if (state_ == State::needs_accept or
      state_ == State::needs_ssl_write_to_accept or
      state_ == State::needs_ssl_read_to_accept)
  {
    try {
      SecureSocket::accept( state_ == State::needs_ssl_write_to_accept );
    }
    catch (const ssl_error & s) {
      switch (s.error_code()) {
      case SSL_ERROR_WANT_READ:
        state_ = State::needs_ssl_read_to_accept;
        break;

      case SSL_ERROR_WANT_WRITE:
        state_ = State::needs_ssl_write_to_accept;
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
  while (not write_buffer_.empty()) {
    const string & data = write_buffer_.front();
    string_view data_view = data;

    unsigned int bytes_written = 0;
    try {
      bytes_written = SecureSocket::write(data_view.substr(buffer_offset_),
                                          state_ == State::needs_ssl_read_to_write);
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

    /* bytes_written > 0 here, so try SSL_write again in the next loop even if
     * partial writes occurred; partial writes are always performed with the
     * size of a message block and don't imply EAGAIN */
    state_ = State::ready;

    if (buffer_offset_ + bytes_written == data_view.length()) {
      write_buffer_.pop_front();
      buffer_offset_ = 0;
    } else {
      buffer_offset_ += bytes_written;
    }
  }
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

unsigned int NBSecureSocket::buffer_bytes() const
{
  unsigned int total_bytes = 0;

  for (const auto & buffer : write_buffer_) {
    total_bytes += buffer.size();
  }

  return total_bytes;
}

void NBSecureSocket::clear_buffer()
{
  write_buffer_.clear();
}
