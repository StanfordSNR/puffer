#include <fcntl.h>

#include <iostream>
#include <stdexcept>
#include <map>

#include "strict_conversions.hh"
#include "socket.hh"
#include "file_descriptor.hh"
#include "exception.hh"
#include "poller.hh"
#include "file_message.hh"

using namespace std;
using namespace PollerShortNames;

void print_usage(const string & program_name)
{
  cerr << "Usage: " << program_name << " PORT" << endl;
}

class Client
{
public:
  Client(TCPSocket && _socket) : socket(move(_socket)), buffer() {}

  void write_to_file() const
  {
    FileMsg metadata(buffer);
    const auto & dst_path = metadata.dst_path;

    FileDescriptor fd(CheckSystemCall("open (" + dst_path + ")",
        open(dst_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644)));
    fd.write(buffer.substr(metadata.size()));
    fd.close();

    cerr << "Received and created " << dst_path << endl;
  }

  TCPSocket socket;
  string buffer;
};

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
  map<uint64_t, Client> clients;

  Poller poller;
  poller.add_action(Poller::Action(listening_socket, Direction::In,
    [&poller, &listening_socket, &global_id, &clients]()->ResultType {
      TCPSocket client_sock = listening_socket.accept();

      /* create a new Client */
      const uint64_t client_id = global_id++;
      clients.emplace(piecewise_construct,
                      forward_as_tuple(client_id),
                      forward_as_tuple(move(client_sock)));

      /* retrieve a client that doesn't go out of scope */
      Client & client = clients.at(client_id);

      poller.add_action(Poller::Action(client.socket, Direction::In,
        [client_id, &client, &clients]()->ResultType {
          const string & data = client.socket.read();
          client.buffer.append(data);

          if (data.empty()) {  // EOF
            client.write_to_file();
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
