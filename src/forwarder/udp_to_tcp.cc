#include <iostream>
#include <string>
#include <cstdint>
#include <deque>

#include "strict_conversions.hh"
#include "socket.hh"
#include "poller.hh"

using namespace std;
using namespace PollerShortNames;

static const int MAX_BUFFER_BYTES = 32 * 1000 * 1000;  /* 32 MB */

void print_usage(const string & program_name)
{
  cerr <<
  "Usage: " << program_name << " UDP-PORT TCP-PORT\n\n"
  "Listen on the TCP port and accept the first connection only. Then start\n"
  "forwarding datagrams from the UDP port to the connected TCP socket"
  << endl;
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

  /* wait for the first connection blockingly */
  TCPSocket client = listening_socket.accept();
  client.set_blocking(false);

  /* create a UDP socket */
  UDPSocket udp_socket;
  udp_socket.bind(Address("0", udp_port));
  udp_socket.set_blocking(false);
  cerr << "Start forwarding datagrams from UDP "
       << udp_socket.local_address().str() << " to TCP "
       << client.peer_address().str() << endl;

  /* start forwarding */
  deque<string> buffer;
  uint64_t buffer_size = 0;
  size_t buffer_offset = 0;

  Poller poller;

  /* read datagrams from UDP socket into the buffer */
  poller.add_action(Poller::Action(udp_socket, Direction::In,
    [&udp_socket, &buffer, &buffer_size]() {
      string data = udp_socket.read();

      if (data.empty()) {
        cerr << "Received empty data from UDP socket" << endl;
        return ResultType::Exit;
      }

      buffer_size += data.size();
      if (buffer_size >= MAX_BUFFER_BYTES) {
        cerr << "Buffer size grows too big: " << buffer_size << endl;
        return ResultType::Exit;
      }

      buffer.emplace_back(move(data));

      return ResultType::Continue;
    }
  ));

  /* write datagrams to TCP client socket from the buffer */
  poller.add_action(Poller::Action(client, Direction::Out,
    [&client, &buffer, &buffer_size, &buffer_offset]() {
      while (not buffer.empty()) {
        const string & data = buffer.front();
        /* convert to string_view to avoid copy */
        string_view data_view = data;

        /* set write_all to false because socket might be unable to write all */
        const auto view_it = client.write(
            data_view.substr(buffer_offset), false);

        if (view_it != data_view.cend()) {
          /* update buffer_size */
          auto new_offset = view_it - data_view.cbegin();
          buffer_size -= new_offset - buffer_offset;

          /* save the offset of the remaining string */
          buffer_offset = new_offset;
          break;
        } else {
          /* update buffer_size */
          buffer_size -= data.size() - buffer_offset;

          /* move onto the next item in the deque */
          buffer_offset = 0;
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
        cerr << "TCP client has closed the connection" << endl;
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
      return ret.exit_status;
    }
  }

  return EXIT_SUCCESS;
}
