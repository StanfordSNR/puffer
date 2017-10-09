#include <iostream>
#include <thread>
#include <getopt.h>
#include "socket.hh"
#include "poller.hh"

#define LOCALHOST "0.0.0.0"
#define TCP_PORT 4000
#define UDP_PORT 5000

using namespace std;
using namespace PollerShortNames;

void print_help(const char *program_name)
{
  cerr << "Usage: " << program_name << " [options] <input>" << endl
    << "-n <arg>, -tcp-hostname <arg>          TCP server hostname" << endl
    << "-t <arg>, -tcp-port <arg>              TCP server port number" << endl
    << "-u <arg>, -udp-port <arg>              UDP port number" << endl;
}

int main(int argc, char* argv[]) {
  int tcp_port = TCP_PORT;
  int udp_port = UDP_PORT;
  string tcp_hostname = LOCALHOST;

  const option command_line_options[] = {
    {"tcp-hostname", required_argument, nullptr, 'n'},
    {"tcp-prot", required_argument, nullptr, 't'},
    {"udp-port", required_argument, nullptr, 'u'},
    {0, required_argument, nullptr, 'h'},
    {0, 0, 0, 0}
  };

  while (true) {
    auto opt = getopt_long(argc, argv, "n:t:u:h", command_line_options, nullptr);
    if (opt == -1)
      break;
    switch (opt){
      case 'n':
        tcp_hostname = optarg;
        break;
      case 't':
        tcp_port = stoi(optarg);
        break;
      case 'u':
        udp_port = stoi(optarg);
        break;
      case 'h':
        print_help(argv[0]);
        return EXIT_FAILURE;
      default:
        print_help(argv[0]);
        return EXIT_FAILURE;
    }
  }
  cout << "Using UDP port " << udp_port << " TCP " << tcp_hostname
    << ":" << tcp_port << endl;

  /* set up udp connection */
  UDPSocket udp = UDPSocket();
  udp.bind(Address(LOCALHOST, udp_port));

  /* set up tcp connection */
  TCPSocket tcp = TCPSocket();
  Address server_address = Address(tcp_hostname, tcp_port);
  cout << "Connecting to TCP " << server_address.str() << endl;
  tcp.connect(server_address);

  cout << "Listening on UDP: " << udp.local_address().str() << endl;

  /* poll from the UDP socket */
  Poller poller;
  poller.add_action(Poller::Action(udp, Direction::In, [&] () {
        pair<Address, std::string> recd = udp.recvfrom();
        string payload = recd.second;
        tcp.write(payload);
        return ResultType::Continue;
        }));

  while (true) {
    const auto ret = poller.poll(-1);
    if (ret.result == Poller::Result::Type::Exit) {
      return ret.exit_status;
    }
  }

}
