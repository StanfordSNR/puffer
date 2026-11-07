#include <fcntl.h>

#include <iostream>
#include <stdexcept>
#include <map>

#include "util.hh"
#include "strict_conversions.hh"
#include "socket.hh"
#include "file_descriptor.hh"
#include "exception.hh"
#include "poller.hh"
#include "filesystem.hh"
#include "file_message.hh"

using namespace std;
using namespace PollerShortNames;

static uint16_t global_file_id = 0;  /* intended to wrap around */
static fs::path tmp_dir_path = fs::temp_directory_path();
static string allowed_origin = "";  /* empty means allow all */

void print_usage(const string & program_name)
{
  cerr <<
  "Usage: " << program_name << " PORT [TMP-DIR] [ALLOWED-ORIGIN]\n\n"
  "TMP-DIR: directory to store temporary files"
  " (must be unique for each file_receiver process)\n"
  "ALLOWED-ORIGIN: allowed IP to accept TCP connections from"
  << endl;
}

class Client
{
public:
  Client(const uint64_t _id, TCPSocket && _socket)
    : id(_id), socket(move(_socket)), buffer() {}

  void write_to_file() const
  {
    if (buffer.empty()) {
      cerr << "Warning: no data received from client " << id << endl;
      return;
    }

    FileMsg metadata(buffer);
    fs::path dst_path = metadata.dst_path;
    fs::path tmp_path = tmp_dir_path / (dst_path.filename().string() + "."
                                        + to_string(global_file_id++));

    /* create parent directories if they don't exist yet */
    if (dst_path.has_parent_path()) {
      fs::create_directories(dst_path.parent_path());
    }
    if (tmp_path.has_parent_path()) {
      fs::create_directories(tmp_path.parent_path());
    }

    FileDescriptor fd(CheckSystemCall("open (" + tmp_path.string() + ")",
        open(tmp_path.string().c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644)));

    /* avoid writing empty data */
    if (buffer.size() > metadata.size()) {
      fd.write(buffer.substr(metadata.size()));
    }

    fd.close();

    fs::rename(tmp_path, dst_path);

    cerr << "Received " << tmp_path << " and moved to " << dst_path << endl;
  }

  uint64_t id;
  TCPSocket socket;
  string buffer;
};

int main(int argc, char * argv[])
{
  if (argc < 1) {
    abort();
  }

  if (argc < 2 or argc > 4) {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  uint16_t port = narrow_cast<uint16_t>(stoi(argv[1]));

  if (argc >= 3) {
    tmp_dir_path = argv[2];
  }

  if (argc == 4) {
    allowed_origin = argv[3];
    cout << "Allowed origin: " << allowed_origin << endl;
  }

  TCPSocket listening_socket;
  listening_socket.set_reuseaddr();
  listening_socket.set_reuseport();
  listening_socket.set_blocking(false);
  listening_socket.bind({"0", port});
  listening_socket.listen(128);
  cerr << "Listening on " << listening_socket.local_address().str() << endl;

  uint64_t global_client_id = 0;
  map<uint64_t, Client> clients;

  Poller poller;
  poller.add_action(Poller::Action(listening_socket, Direction::In,
    [&poller, &listening_socket, &global_client_id, &clients]()->ResultType {
      Address peer_addr;
      TCPSocket client_sock = listening_socket.accept(peer_addr);

      /* check if the peer has gone, e.g., it reset the connection right away */
      try {
        client_sock.peer_address();
      } catch (const exception & e) {
        cerr << "[" << date_time() << "] Peer " << peer_addr.str()
             << " has gone: " << e.what() << endl;
        return ResultType::Continue;
      }

      /* check allowed origin */
      if (not allowed_origin.empty() and peer_addr.ip() != allowed_origin) {
        cout << "[" << date_time() << "] Rejected connection from "
             << peer_addr.str() << endl;
        return ResultType::Continue;
      }

      /* create a new Client */
      const uint64_t client_id = global_client_id++;
      clients.emplace(piecewise_construct,
                      forward_as_tuple(client_id),
                      forward_as_tuple(client_id, move(client_sock)));

      /* retrieve a client that doesn't go out of scope */
      Client & client = clients.at(client_id);

      poller.add_action(Poller::Action(client.socket, Direction::In,
        [&client, &clients]()->ResultType {
          const string & data = client.socket.read();

          if (data.empty()) {  // EOF
            try {
              client.write_to_file();
            } catch (const exception & e) {
              cerr << "Warning: ignoring client " << client.id << ": "
                   << e.what() << endl;
            }
            clients.erase(client.id);
            return ResultType::CancelAll;
          }

          client.buffer.append(data);

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
