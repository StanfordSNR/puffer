#include <iostream>
#include <string>
#include <cstdint>
#include <deque>

#include "strict_conversions.hh"
#include "socket.hh"
#include "poller.hh"

using namespace std;
using namespace PollerShortNames;

static const int ALERT_BUFFER_BYTES = 100 * 1000 * 1000;  /* 100 MB */

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

  Poller poller;

  /* read datagrams from UDP socket into the buffer */
  poller.add_action(Poller::Action(udp_socket, Direction::In,
    [&udp_socket, &buffer, &buffer_size]() {
      string data = udp_socket.read();

      /* assert that there isn't nothing to read */
      assert(not udp_socket.eof());

      buffer_size += data.size();
      if (buffer_size >= ALERT_BUFFER_BYTES) {
        cerr << "ALERT: buffer size growing too big: " << buffer_size << endl;
      }

      buffer.emplace_back(move(data));

      return ResultType::Continue;
    }
  ));

  /* write datagrams to TCP client socket from the buffer */
  poller.add_action(Poller::Action(client, Direction::Out,
    [&client, &buffer, &buffer_size]() {
      /* assert that the callback is active only when buffer is not empty */
      assert(not buffer.empty());

      const string & data = buffer.front();
      buffer_size -= data.size();

      client.write(data);

      buffer.pop_front();

      return ResultType::Continue;
    },
    /* interested only when buffer is not empty */
    [&buffer]() { return not buffer.empty(); }
  ));

  for (;;) {
    auto ret = poller.poll(-1);
    if (ret.result != Poller::Result::Type::Success) {
      return ret.exit_status;
    }
  }

  return EXIT_SUCCESS;
}
