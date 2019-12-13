/* -*-mode:c++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */

#include "ws_server.hh"

#include <iostream>
#include <stdexcept>
#include <crypto++/sha.h>
#include <crypto++/hex.h>
#include <crypto++/base64.h>

#include "http_response.hh"
#include "exception.hh"

using namespace std;
using namespace PollerShortNames;
using namespace CryptoPP;

static string WS_MAGIC_STRING = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

bool is_valid_handshake_request(const HTTPRequest & request)
{
  string first_line = request.first_line();

  if (first_line.substr(0, 3) != "GET") {
    cerr << "Invalid WebSocket request: method must be GET" << endl;
    return false;
  }

  auto last_space = first_line.rfind(" ");
  if (last_space == string::npos) { return false; }

  if (first_line.substr(last_space + 1) != "HTTP/1.1" and
      first_line.substr(last_space + 1) != "HTTP/2") {
    cerr << "Invalid WebSocket request: only allow HTTP/1.1 and HTTP/2" << endl;
    return false;
  }

  if (not request.has_header("Connection") or
      request.get_header_value("Connection").find("Upgrade") == string::npos) {
    cerr << "Invalid WebSocket request: 'Connection: Upgrade' is required" << endl;
    return false;
  }

  if (not request.has_header("Upgrade") or
      request.get_header_value("Upgrade") != "websocket") {
    cerr << "Invalid WebSocket request: 'Upgrade: websocket' is required" << endl;
    return false;
  }

  /* require Sec-WebSocket-Key to protect against abuse */
  if (not request.has_header("Sec-WebSocket-Key")) {
    cerr << "Invalid WebSocket request: 'Sec-WebSocket-Key' is required" << endl;
    return false;
  }

  return true;
}

HTTPResponse create_handshake_response(const HTTPRequest & request)
{
  HTTPResponse response;
  response.set_request(request);

  /* send "400 Bad Request" for invalid WebSocket handshake request */
  if (not is_valid_handshake_request(request)) {
    response.set_first_line("HTTP/1.1 400 Bad Request");
    response.add_header(HTTPHeader{"Content-Length", "0"});
    response.add_header(HTTPHeader{"Connection", "close"});
    response.done_with_headers();
    response.read_in_body("");
    return response;
  }

  /* reject requests without Origin (maybe check for same origin later) */
  if (not request.has_header("Origin")) {
    response.set_first_line("HTTP/1.1 403 Forbidden");
    response.add_header(HTTPHeader{"Content-Length", "0"});
    response.add_header(HTTPHeader{"Connection", "close"});
    response.done_with_headers();
    response.read_in_body("");
    return response;
  }

  /* compute the value of Sec-WebSocket-Accept based on Sec-WebSocket-Key */
  string sec_key = request.get_header_value("Sec-WebSocket-Key");
  string sec_accept;
  CryptoPP::SHA1 sha1_function;
  StringSource s( sec_key + WS_MAGIC_STRING, true,
                  new HashFilter( sha1_function,
                                  new Base64Encoder( new StringSink( sec_accept ),
                                                     false ) ) );

  /* accept WebSocket request */
  response.set_first_line("HTTP/1.1 101 Switching Protocols");
  response.add_header(HTTPHeader{"Connection", "Upgrade"});
  response.add_header(HTTPHeader{"Upgrade", "websocket"});
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
  : socket(ssl_context.new_secure_socket(move(sock)))
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
    const string & buffer = send_buffer.front();

    /* need to convert to string_view iterator to avoid copy */
    string_view buffer_view = buffer;

    /* set write_all to false because socket might be unable to write all */
    const auto view_it = socket.write(
        buffer_view.substr(send_buffer_offset), false);

    if (view_it != buffer_view.cend()) {
      /* save the offset of the remaining string */
      send_buffer_offset = view_it - buffer_view.cbegin();
      break;
    } else {
      /* move onto the next item in the deque */
      send_buffer_offset = 0;
      send_buffer.pop_front();
    }
  }
}

template<>
void WSServer<NBSecureSocket>::Connection::write()
{
  while (not send_buffer.empty()) {
    socket.ezwrite(move(send_buffer.front()));
    send_buffer.pop_front();
  }
}

template<class SocketType>
void WSServer<SocketType>::init_listener_socket()
{
  listener_socket_ = TCPSocket();
  listener_socket_.set_blocking(false);
  listener_socket_.set_reuseaddr();
  listener_socket_.set_reuseport();
  listener_socket_.set_congestion_control(congestion_control_);
  listener_socket_.bind(listener_addr_);
  listener_socket_.listen();

  poller_.add_action(Poller::Action(listener_socket_, Direction::In,
    [this]()->ResultType
    {
      TCPSocket client = listener_socket_.accept();
      client.set_blocking(false);

      const uint64_t conn_id = last_connection_id_++;
      connections_.emplace(piecewise_construct,
                           forward_as_tuple(conn_id),
                           forward_as_tuple(move(client), ssl_context_));
      Connection & conn = connections_.at(conn_id);

      /* add the actions for this connection */
      poller_.add_action(Poller::Action(conn.socket, Direction::In,
        [this, &conn, conn_id]()->ResultType
        {
          const string data = conn.read();

          if (data.empty()) {
            /* peer socket is gone */
            force_close_connection(conn_id);
            return ResultType::CancelAll;
          }

          if (conn.state == Connection::State::NotConnected) {
            try {
              conn.ws_handshake_parser.parse(data);
            } catch (const exception & e) {
              /* close the connection if received an invalid message */
              print_exception("ws_server", e);
              force_close_connection(conn_id);
              return ResultType::CancelAll;
            }

            while (not conn.ws_handshake_parser.empty()) {
              auto request = move(conn.ws_handshake_parser.front());
              conn.ws_handshake_parser.pop();

              const auto & response = create_handshake_response(request);
              conn.send_buffer.emplace_back(response.str());

              /* only continue with status code of 101 */
              if (response.status_code() != "101") {
                /* TODO: response will not reach the client side currently */
                force_close_connection(conn_id);
                return ResultType::CancelAll;
              }

              conn.state = Connection::State::Connecting;
            }
          }
          else if (conn.state == Connection::State::Connected) {
            try {
              conn.ws_message_parser.parse(data);
            } catch (const exception & e) {
              /* close the connection if received an invalid message */
              print_exception("ws_server", e);
              wait_close_connection(conn_id);
            }

            while (not conn.ws_message_parser.empty()) {
              WSMessage message = move(conn.ws_message_parser.front());
              conn.ws_message_parser.pop();

              switch (message.type()) {
              case WSMessage::Type::Text:
              case WSMessage::Type::Binary:
                message_callback_(conn_id, message);
                break;

              case WSMessage::Type::Close:
              {
                /* respond to client-initiated close */
                WSFrame close_frame { true, WSFrame::OpCode::Close,
                                      message.payload() };
                queue_frame(conn_id, close_frame);
                force_close_connection(conn_id);
                return ResultType::CancelAll;
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
                assert(false);  /* will not happen */
                break;
              }
            }
          }
          else if (conn.state == Connection::State::Closing) {
            try {
              conn.ws_message_parser.parse(data);
            } catch (const exception & e) {
              /* close the connection if received an invalid message */
              print_exception("ws_server", e);
              force_close_connection(conn_id);
              return ResultType::CancelAll;
            }

            while (not conn.ws_message_parser.empty()) {
              WSMessage message = move(conn.ws_message_parser.front());
              conn.ws_message_parser.pop();

              switch (message.type()) {
              case WSMessage::Type::Close:
                /* complete server-initiated close */
                force_close_connection(conn_id);
                return ResultType::CancelAll;

              default:
                /* all the other message types are ignored */
                break;
              }
            }
          } else {
            cerr << "Invalid conn.state = " << (int) conn.state << endl;
            force_close_connection(conn_id);
            return ResultType::CancelAll;
          }

          return ResultType::Continue;
        },
        [&conn]()->bool
        {
          return (conn.state != Connection::State::Connecting) and
                 (conn.state != Connection::State::Closed);
        }
      ));

      poller_.add_action(Poller::Action(conn.socket, Direction::Out,
        [this, &conn, conn_id]()->ResultType
        {
          if (conn.state == Connection::State::Connecting) {
            if (conn.data_to_write()) {
              conn.write();
            }

            if (not conn.data_to_write()) {
              /* if we've sent the whole handshake response */
              conn.state = Connection::State::Connected;
              open_callback_(conn_id);
            }
          }
          else if ((conn.state == Connection::State::Connected or
                    conn.state == Connection::State::Closing or
                    conn.state == Connection::State::Closed) and
                   conn.data_to_write()) {
            conn.write();
          }

          if (conn.state == Connection::State::Closed and
              not conn.data_to_write()) {
            force_close_connection(conn_id);
            return ResultType::CancelAll;
          }

          return ResultType::Continue;
        },
        [&conn]()->bool
        {
          return (conn.state == Connection::State::Connecting) or
                 ((conn.state == Connection::State::Connected or
                   conn.state == Connection::State::Closing or
                   conn.state == Connection::State::Closed) and
                  conn.interested_in_sending());
        }
      ));

      return ResultType::Continue;
    }
  ));
}

template<class SocketType>
WSServer<SocketType>::WSServer(const Address & listener_addr,
                               const string & congestion_control)
{
  listener_addr_ = listener_addr;
  congestion_control_ = congestion_control;
  init_listener_socket();
}

template<class SocketType>
bool WSServer<SocketType>::queue_frame(const uint64_t connection_id,
                                       const WSFrame & frame)
{
  Connection & conn = connections_.at(connection_id);

  if (conn.state != Connection::State::Connected) {
    cerr << connection_id << ": not connected; cannot queue frame" << endl;
    return false;
  }

  /* frame.to_string() inevitably copies frame.payload_ into the return string,
   * but the return string will be moved into conn.send_buffer without copy */
  conn.send_buffer.emplace_back(frame.to_string());
  return true;
}

template<class SocketType>
void WSServer<SocketType>::wait_close_connection(const uint64_t connection_id)
{
  auto conn_it = connections_.find(connection_id);
  /* do nothing if the connection no longer exists */
  if (conn_it == connections_.end()) {
    return;
  }

  auto & conn = conn_it->second;
  if (conn.state != Connection::State::Connected) {
    cerr << "Warning: wait_close_connection is called but not connected" << endl;
    return;
  }

  /* try to close the connection gracefully */
  WSFrame close_frame { true, WSFrame::OpCode::Close, "" };
  queue_frame(connection_id, close_frame);
  conn.state = Connection::State::Closing;
}

template<class SocketType>
void WSServer<SocketType>::force_close_connection(const uint64_t connection_id)
{
  auto conn_it = connections_.find(connection_id);
  /* do nothing if the connection no longer exists */
  if (conn_it == connections_.end()) {
    return;
  }

  auto & conn = conn_it->second;
  conn.state = Connection::State::Closed;
  closed_connections_.insert(connection_id);
  close_callback_(connection_id);
}

template<class SocketType>
void WSServer<SocketType>::close_connection(const uint64_t connection_id)
{
  wait_close_connection(connection_id);
}

template<class SocketType>
void WSServer<SocketType>::clean_idle_connection(const uint64_t connection_id)
{
  auto conn_it = connections_.find(connection_id);
  /* do nothing if the connection no longer exists */
  if (conn_it == connections_.end()) {
    return;
  }

  auto & conn = conn_it->second;

  /* deregister the socket in the connection from the poller */
  poller_.remove_fd(conn.socket.fd_num());

  conn.state = Connection::State::Closed;
  closed_connections_.insert(connection_id);
  close_callback_(connection_id);
}

template<class SocketType>
TCPInfo WSServer<SocketType>::get_tcp_info(const uint64_t connection_id) const
{
  const Connection & conn = connections_.at(connection_id);

  return conn.socket.get_tcp_info();
}

template<class SocketType>
Address WSServer<SocketType>::peer_addr(const uint64_t connection_id) const
{
  const Connection & conn = connections_.at(connection_id);

  return conn.socket.peer_address();
}

template<>
bool WSServer<TCPSocket>::Connection::interested_in_sending() const
{
  return send_buffer.size() > 0;
}

template<>
bool WSServer<NBSecureSocket>::Connection::interested_in_sending() const
{
  return send_buffer.size() > 0 or socket.something_to_write();
}

template<>
unsigned int WSServer<TCPSocket>::Connection::buffer_bytes() const
{
  unsigned int total_bytes = 0;
  for (const auto & buffer : send_buffer) {
    total_bytes += buffer.size();
  }

  return total_bytes;
}

template<>
unsigned int WSServer<NBSecureSocket>::Connection::buffer_bytes() const
{
  unsigned int total_bytes = 0;
  for (const auto & buffer : send_buffer) {
    total_bytes += buffer.size();
  }

  /* NBSecureSocket maintains another buffer by itself */
  total_bytes += socket.buffer_bytes();

  return total_bytes;
}

template<class SocketType>
unsigned int WSServer<SocketType>::buffer_bytes(const uint64_t conn_id) const
{
  return connections_.at(conn_id).buffer_bytes();
}

template<>
void WSServer<TCPSocket>::Connection::clear_buffer()
{
  send_buffer.clear();
}

template<>
void WSServer<NBSecureSocket>::Connection::clear_buffer()
{
  send_buffer.clear();
  socket.clear_buffer();
}

template<class SocketType>
void WSServer<SocketType>::clear_buffer(const uint64_t conn_id)
{
  return connections_.at(conn_id).clear_buffer();
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

template<class SocketType>
int WSServer<SocketType>::loop()
{
  for (;;) {
    auto ret = loop_once();

    if (ret.result != Poller::Result::Type::Success) {
      return ret.exit_status;
    }
  }
}

template class WSServer<TCPSocket>;
template class WSServer<NBSecureSocket>;
