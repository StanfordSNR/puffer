#include <iostream>
#include <string>
#include <stdexcept>
#include <getopt.h>
#include <tuple>

#include "socket.hh"

using namespace std;

void print_usage(const string & program_name)
{
  cerr << "Usage: " << program_name << " [options]" << endl
       << endl
       << "Options:" << endl
       << "--udp-port PORT        "
       << "UDP port on localhost to receive datagrams from" << endl
       << "--tcp HOSTNAME:PORT    "
       << "TCP host to forward datagrams to" << endl;
}

tuple<string, string> parse_host(const string & host)
{
  auto pos = host.find(':');

  if (pos == string::npos) {
    throw runtime_error("Invalid host format! Should be HOSTNAME:PORT");
  }

  return make_tuple(host.substr(0, pos), host.substr(pos + 1));
}

int main(int argc, char * argv[])
{
  if (argc < 1) {
    abort();
  }

  string udp_hostname{"0"}, udp_port;
  string tcp_hostname, tcp_port;

  const option cmd_line_opts[] = {
    {"udp-port", required_argument, nullptr, 'u'},
    {"tcp",      required_argument, nullptr, 't'},
    { nullptr,   0,                 nullptr,  0 }
  };

  while (true) {
    const int opt = getopt_long(argc, argv, "u:t:", cmd_line_opts, nullptr);
    if (opt == -1) {
      break;
    }

    switch (opt) {
    case 'u':
      udp_port = optarg;
      break;
    case 't':
      tie(tcp_hostname, tcp_port) = parse_host(optarg);
      break;
    default:
      print_usage(argv[0]);
      return EXIT_FAILURE;
    }
  }

  if (optind != argc) { /* if there are any positional arguments */
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  if (udp_port.empty() or tcp_hostname.empty() or tcp_port.empty()) {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  UDPSocket udp_sock;
  udp_sock.bind(Address(udp_hostname, udp_port));
  cerr << "Listening on UDP host " << udp_sock.local_address().str() << endl;

  TCPSocket tcp_sock;
  tcp_sock.connect(Address(tcp_hostname, tcp_port));
  cerr << "Connected to TCP host " << tcp_sock.peer_address().str() << endl;

  while (true) {
    pair<Address, string> addr_payload = udp_sock.recvfrom();
    string payload = addr_payload.second;

    if (not payload.empty()) {
      tcp_sock.write(payload);
    }
  }

  return EXIT_SUCCESS;
}
