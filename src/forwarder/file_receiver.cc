#include <iostream>
#include <stdexcept>
#include <map>

#include "strict_conversions.hh"
#include "socket.hh"
#include "poller.hh"
#include "file_message.hh"

using namespace std;
using namespace PollerShortNames;

void print_usage(const string & program_name)
{
  cerr << "Usage: " << program_name << " PORT" << endl;
}

int main(int argc, char * argv[])
{
  if (argc < 1) {
    abort();
  }

  if (argc != 2) {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  uint16_t port = narrow_cast<uint16_t>(stoi(argv[1]));

  TCPSocket listening_socket;
  listening_socket.set_reuseaddr();
  listening_socket.set_reuseport();
  listening_socket.set_blocking(false);
  listening_socket.bind({"0", port});
  listening_socket.listen();
  cerr << "Listening on " << listening_socket.local_address().str() << endl;

  uint64_t global_id = 0;
  map<uint64_t, TCPSocket> clients;

  Poller poller;
  poller.add_action(Poller::Action(listening_socket, Direction::In,
    [&poller, &listening_socket, &global_id, &clients]()->ResultType {
      TCPSocket tmp_client = listening_socket.accept();

      /* insert tmp_client into clients */
      const uint64_t client_id = global_id++;
      clients.emplace(piecewise_construct,
                      forward_as_tuple(client_id),
                      forward_as_tuple(move(tmp_client)));

      /* retrieve a client that doesn't go out of scope */
      TCPSocket & client = clients.at(client_id);

      poller.add_action(Poller::Action(client, Direction::In,
        [client_id, &client, &clients]()->ResultType {
          const string data = client.read();

          // TODO
          cerr << data << endl;

          if (data.empty()) {
            clients.erase(client_id);
            return ResultType::CancelAll;
          }

          return ResultType::Continue;
        }
      ));

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
