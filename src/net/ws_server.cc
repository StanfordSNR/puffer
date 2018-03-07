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

  SHA1 sha1_function;

  StringSource s( sec_key + WS_MAGIC_STRING, true,
                  new HashFilter( sha1_function,
                                  new Base64Encoder( new StringSink( sec_accept ), false ) ) );


  HTTPResponse response;
  response.set_first_line("HTTP/1.1 101 Switching Protocols");
  response.add_header(HTTPHeader{"Upgrade", "websocket"});
  response.add_header(HTTPHeader{"Connection", "Upgrade"});
  response.add_header(HTTPHeader{"Sec-WebSocket-Accept", sec_accept});
  response.done_with_headers();
  response.read_in_body("");

  return response;
}

WSServer::WSServer(const Address & listener_addr)
  : listener_socket_()
{
  listener_socket_.set_blocking(false);
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
      connections_.emplace(make_pair(conn_id, std::move(client)));
      Connection & conn = connections_.at(conn_id);

      /* add the actions for this connection */
      poller_.add_action(Poller::Action(conn.socket, Direction::In,
        [this, &conn, conn_id] () -> ResultType
        {
          string data = conn.socket.read();

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
                break;

              case WSMessage::Type::Ping:
              {
                WSFrame pong { true, WSFrame::OpCode::Pong, "" };
                queue_frame(conn_id, pong);
                break;
              }

              case WSMessage::Type::Pong:
                break;

              default:
                throw runtime_error("unhandled message type");
              }
            }
          }
          else {
            throw runtime_error("unhandled data");
          }

          return ResultType::Continue;
        },
        [&conn] () -> bool
        {
          return (conn.state != Connection::State::Connecting);
        }
      ));

      poller_.add_action(Poller::Action(conn.socket, Direction::Out,
        [this, &conn, conn_id] () -> ResultType
        {
          if (conn.state == Connection::State::Connecting) {
            /* okay, we should prepare the handshake response now */
            conn.socket.write(create_handshake_response(conn.handshake_request).str());
            conn.state = Connection::State::Connected;
            open_callback_(conn_id);
          }
          else if (conn.state == Connection::State::Connected and conn.data_to_send()) {
            string::const_iterator last_write = conn.socket.write(conn.send_buffer.begin(), conn.send_buffer.end());
            conn.send_buffer.erase(conn.send_buffer.begin(), last_write);
          }

          return ResultType::Continue;
        },
        [&conn] () -> bool
        {
          return (conn.state == Connection::State::Connecting) or
                 (conn.state == Connection::State::Connected and
                  conn.data_to_send());
        }
      ));

      return ResultType::Continue;
    }
  ));
}

void WSServer::queue_frame(const uint64_t connection_id, const WSFrame & frame)
{
  if (connections_.count(connection_id) == 0) {
    throw runtime_error("invalid connection id: " + to_string(connection_id));
  }

  Connection & conn = connections_.at(connection_id);

  if (conn.state != Connection::State::Connected) {
    throw runtime_error("not connected, cannot send the frame");
  }

  conn.send_buffer += frame.to_string();
}

void WSServer::close_connection(const uint64_t connection_id)
{
  if (connections_.count(connection_id) == 0) {
    throw runtime_error("invalid connection id: " + to_string(connection_id));
  }

  Connection & conn = connections_.at(connection_id);

  if (conn.state != Connection::State::Connected) {
    throw runtime_error("not connected, cannot close the connection");
  }

  WSFrame close_frame { true, WSFrame::OpCode::Close, "" };
  queue_frame(connection_id, close_frame);
  conn.state = Connection::State::Closing;
}

Poller::Result WSServer::loop_once()
{
  return poller_.poll(-1);
}
