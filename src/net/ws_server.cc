/* -*-mode:c++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */

#include "ws_server.hh"

#include <iostream>
#include <stdexcept>
#include <crypto++/sha.h>
#include <crypto++/hex.h>
#include <crypto++/base64.h>

#include "http_response.hh"

using namespace std;
using namespace PollerShortNames;
using namespace CryptoPP;

static string WS_MAGIC_STRING = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

HTTPResponse create_handshake_response(const HTTPRequest & request)
{
  /* TODO check if it is really a websocket connection */
  string sec_key = request.get_header_value("Sec-WebSocket-Key");
  string sec_version = request.get_header_value("Sec-WebSocket-Version");
  string sec_accept;

  CryptoPP::SHA1 sha1_function;

  StringSource s( sec_key + WS_MAGIC_STRING, true,
                  new HashFilter( sha1_function,
                                  new Base64Encoder( new StringSink( sec_accept ),
                                                     false ) ) );


  HTTPResponse response;
  response.set_first_line("HTTP/1.1 101 Switching Protocols");
  response.add_header(HTTPHeader{"Upgrade", "websocket"});
  response.add_header(HTTPHeader{"Connection", "Upgrade"});
  response.add_header(HTTPHeader{"Sec-WebSocket-Accept", sec_accept});
  response.done_with_headers();
  response.read_in_body("");

  return response;
}

template<>
WSServer<TCPSocket>::Connection::Connection(TCPSocket && sock, SSLContext &)
  : socket(move(sock))
{}

template<>
WSServer<NBSecureSocket>::Connection::Connection(TCPSocket && sock,
                                                 SSLContext & ssl_context)
  : socket(move(ssl_context.new_secure_socket(move(sock))))
{
  socket.accept();
}

template<>
string WSServer<TCPSocket>::Connection::read()
{
  return socket.read();
}

template<>
string WSServer<NBSecureSocket>::Connection::read()
{
  return socket.ezread();
}

template<>
void WSServer<TCPSocket>::Connection::write()
{
  while (not send_buffer.empty()) {
    string & buffer = send_buffer.front();

    auto it = socket.write(buffer);
    if (it != buffer.cend()) {
      buffer.erase(0, it - buffer.cbegin());
      break;
    } else {
      send_buffer.pop_front();
    }
  }
}

template<>
void WSServer<NBSecureSocket>::Connection::write()
{
  for (string & buffer : send_buffer) {
    socket.ezwrite(move(buffer));
  }

  send_buffer.clear();
}

template<class SocketType>
unsigned int WSServer<SocketType>::Connection::buffer_bytes() const
{
  unsigned int total_bytes = 0;

  for (const auto & buffer : send_buffer) {
    total_bytes += buffer.size();
  }

  return total_bytes;
}

template<class SocketType>
WSServer<SocketType>::WSServer(const Address & listener_addr)
  : listener_socket_()
{
  listener_socket_.set_blocking(false);
  listener_socket_.set_reuseaddr();
  listener_socket_.bind(listener_addr);
  listener_socket_.listen();

  poller_.add_action(Poller::Action(listener_socket_, Direction::In,
    [this] () -> ResultType
    {
      /* incoming connection */
      TCPSocket client = listener_socket_.accept();

      /* let's make the socket non-blocking */
      client.set_blocking(false);

      const uint64_t conn_id = last_connection_id_++;
      connections_.emplace(piecewise_construct,
                           forward_as_tuple(conn_id),
                           forward_as_tuple(move(client), ref(ssl_context_)));
      Connection & conn = connections_.at(conn_id);

      /* add the actions for this connection */
      poller_.add_action(Poller::Action(conn.socket, Direction::In,
        [this, &conn, conn_id] () -> ResultType
        {
          const string data = conn.read();

          if (conn.state == Connection::State::NotConnected) {
            conn.ws_handshake_parser.parse(data);

            if (not conn.ws_handshake_parser.empty()) {
              conn.handshake_request = conn.ws_handshake_parser.front();
              conn.ws_handshake_parser.pop();
              conn.state = Connection::State::Connecting;
            }
          }
          else if (conn.state == Connection::State::Connected) {
            conn.ws_message_parser.parse(data);

            if (not conn.ws_message_parser.empty()) {
              WSMessage message = conn.ws_message_parser.front();
              conn.ws_message_parser.pop();

              switch (message.type()) {
              case WSMessage::Type::Text:
              case WSMessage::Type::Binary:
                message_callback_(conn_id, message);
                break;

              case WSMessage::Type::Close:
              {
                WSFrame close { true, WSFrame::OpCode::Close, message.payload() };
                queue_frame(conn_id, close);
                conn.state = Connection::State::Closed;
                break;
              }

              case WSMessage::Type::Ping:
              {
                WSFrame pong { true, WSFrame::OpCode::Pong, "" };
                queue_frame(conn_id, pong);
                break;
              }

              case WSMessage::Type::Pong:
                break;

              default:
                cerr << "unhandled message type" << endl;
                close_connection(conn_id);
              }
            }
          }
          else if (conn.state == Connection::State::Closing) {
            conn.ws_message_parser.parse(data);
            if (not conn.ws_message_parser.empty()) {
              WSMessage message = conn.ws_message_parser.front();
              conn.ws_message_parser.pop();

              switch (message.type()) {
              case WSMessage::Type::Close:
                conn.state = Connection::State::Closed;
                conn.send_buffer.clear();

                /* we don't want to poll on this socket anymore */
                close_callback_(conn_id);
                closed_connections_.insert(conn_id);
                return ResultType::CancelAll;

              default:
                /* all the other message types are ignored */
                break;
              }
            }
          }
          else {
            /* TODO: this needs to clean-up the connection
             *    cerr << "unhandled data" << endl;
             *    close_connection(conn_id);
             * The above code will throw. */
            throw runtime_error("unhandled data");
          }

          return ResultType::Continue;
        },
        [&conn] () -> bool
        {
          return (conn.state != Connection::State::Connecting) and
                 (conn.state != Connection::State::Closed);
        }
      ));

      poller_.add_action(Poller::Action(conn.socket, Direction::Out,
        [this, &conn, conn_id] () -> ResultType
        {
          if (conn.state == Connection::State::Connecting) {
            /* okay, we should prepare the handshake response now */
            if (conn.data_to_send()) {
              conn.write();
            }
            else {
              conn.send_buffer.emplace_back(
                  create_handshake_response(conn.handshake_request).str());
              conn.write();
            }

            /* if we've sent the whole handshake response */
            if (not conn.data_to_send()) {
              conn.state = Connection::State::Connected;
              open_callback_(conn_id);
            }
          }
          else if ((conn.state == Connection::State::Connected or
                    conn.state == Connection::State::Closing or
                    conn.state == Connection::State::Closed) and
                   conn.data_to_send()) {
            conn.write();
          }

          if (conn.state == Connection::State::Closed and
              not conn.data_to_send()) {
            close_callback_(conn_id);
            closed_connections_.insert(conn_id);
            return ResultType::CancelAll;
          }

          return ResultType::Continue;
        },
        [&conn] () -> bool
        {
          return (conn.state == Connection::State::Connecting) or
                 ((conn.state == Connection::State::Connected or
                   conn.state == Connection::State::Closing or
                   conn.state == Connection::State::Closed) and
                   conn.data_to_send());
        }
      ));

      return ResultType::Continue;
    }
  ));
}

template<class SocketType>
void WSServer<SocketType>::queue_frame(const uint64_t connection_id,
                                       const WSFrame & frame)
{
  Connection & conn = connections_.at(connection_id);

  if (conn.state != Connection::State::Connected) {
    throw runtime_error("not connected, cannot send the frame");
  }

  /* frame.to_string() inevitably copies frame.payload_ into the return string,
   * but the return string will be moved into conn.send_buffer without copy */
  conn.send_buffer.emplace_back(frame.to_string());
}

template<class SocketType>
void WSServer<SocketType>::close_connection(const uint64_t connection_id)
{
  Connection & conn = connections_.at(connection_id);

  if (conn.state != Connection::State::Connected) {
    throw runtime_error("not connected, cannot close the connection");
  }

  WSFrame close_frame { true, WSFrame::OpCode::Close, "" };
  queue_frame(connection_id, close_frame);
  conn.state = Connection::State::Closing;
}

template<>
unsigned int WSServer<TCPSocket>::queue_bytes(const uint64_t conn_id) const
{
  const Connection & conn = connections_.at(conn_id);

  return conn.buffer_bytes();
}

template<>
unsigned int WSServer<NBSecureSocket>::queue_bytes(const uint64_t conn_id) const
{
  const Connection & conn = connections_.at(conn_id);

  unsigned int total_size = conn.buffer_bytes();
  total_size += conn.socket.buffer_bytes();

  return total_size;
}

template<class SocketType>
Poller::Result WSServer<SocketType>::loop_once()
{
  auto result = poller_.poll(-1);

  /* let's garbage collect the closed connections */
  for (const uint64_t conn_id : closed_connections_) {
    connections_.erase(conn_id);
  }

  closed_connections_.clear();

  return result;
}

template class WSServer<TCPSocket>;
template class WSServer<NBSecureSocket>;
