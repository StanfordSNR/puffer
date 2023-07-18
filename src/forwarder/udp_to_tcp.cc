#include <iostream>
#include <string>
#include <cstdint>
#include <deque>

#include "strict_conversions.hh"
#include "socket.hh"
#include "poller.hh"
#include "util.hh"

using namespace std;
using namespace PollerShortNames;

void print_usage(const string & program_name)
{
  cerr <<
  "Usage: " << program_name << " UDP-PORT TCP-PORT\n\n"
  "Listen on the TCP port and accept the first connection only. Then start\n"
  "forwarding datagrams from the UDP port to the connected TCP socket"
  << endl;
}

void accept_one_client(TCPSocket & listening_socket, const uint16_t udp_port)
{
  Poller poller;

  /* create a UDP socket */
  UDPSocket udp_socket;
  udp_socket.bind(Address("0", udp_port));
  udp_socket.set_blocking(false);

  /* wait for the first connection blockingly */
  TCPSocket client = listening_socket.accept();
  client.set_blocking(false);

  cerr << "Start forwarding datagrams from UDP "
       << udp_socket.local_address().str() << " to TCP "
       << client.peer_address().str() << endl;

  /* start forwarding */
  deque<string> buffer;
  uint64_t buffer_size = 0;

  /* read datagrams from UDP socket into the buffer */
  poller.add_action(Poller::Action(udp_socket, Direction::In,
    [&udp_socket, &udp_port, &buffer, &buffer_size, &client]() {
      while (true) {
        const auto [ignore, data] = udp_socket.recvfrom();

        if (not data) { // EWOULDBLOCK; try again when data is available
          break;
        }

        buffer_size += data->size();

        buffer.emplace_back(move(*data));

        if (buffer_size > 50 * 1024 * 1024) { // 50 MB
          /* try to gracefully close sockets */
          client.close();
          udp_socket.close();

          throw runtime_error("Error: give up forwarding data");
        } else if (buffer_size > 10 * 1024 * 1024) { // 10 MB
          cerr << "[" << date_time() << "] "
               << "port=" << udp_port << ", "
               << "buffer=" << buffer_size << endl;
        }
      }

      return ResultType::Continue;
    }
  ));

  /* write datagrams to TCP client socket from the buffer */
  poller.add_action(Poller::Action(client, Direction::Out,
    [&client, &buffer, &buffer_size]() {
      while (not buffer.empty()) {
        string & data = buffer.front();

        const size_t bytes_written = client.nb_write(data);
        if (bytes_written == 0) { // EWOULDBLOCK
          break;
        }

        buffer_size -= bytes_written;

        if (bytes_written < data.size()) { // partial write
          data.erase(0, bytes_written);
        } else { // full write
          buffer.pop_front();
        }
      }

      return ResultType::Continue;
    },
    /* interested only when buffer is not empty */
    [&buffer]() {
      return not buffer.empty();
    }
  ));

  /* check if TCP client socket has closed */
  poller.add_action(Poller::Action(client, Direction::In,
    [&client]() {
      const string data = client.read();

      if (data.empty()) {
        cerr << "Error: TCP client has closed the connection" << endl;
        return ResultType::Exit;
      } else {
        cerr << "Warning: ignoring data received from TCP client" << endl;
      }

      return ResultType::Continue;
    }
  ));

  for (;;) {
    auto ret = poller.poll(-1);
    if (ret.result != Poller::Result::Type::Success) {
      return;
    }
  }
}

int main(int argc, char * argv[])
{
  if (argc < 1) {
    abort();
  }

  if (argc != 3) {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  uint16_t udp_port = narrow_cast<uint16_t>(stoi(argv[1]));
  uint16_t tcp_port = narrow_cast<uint16_t>(stoi(argv[2]));

  /* create a TCP socket */
  TCPSocket listening_socket;
  listening_socket.set_reuseaddr();
  listening_socket.bind(Address("0", tcp_port));
  listening_socket.listen();
  cerr << "Listening on TCP " << listening_socket.local_address().str() << endl;

  for (;;) {
    accept_one_client(listening_socket, udp_port);
    cerr << "Error recovery: waiting to accept a new TCP connection" << endl;
  }

  return EXIT_SUCCESS;
}
