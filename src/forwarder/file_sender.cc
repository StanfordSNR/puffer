#include <iostream>

#include "strict_conversions.hh"
#include "file_message.hh"
#include "socket.hh"

using namespace std;

void print_usage(const string & program_name)
{
  cerr << "Usage: " << program_name << " SRC-PATH HOST PORT DST-PATH" << endl;
}

int main(int argc, char * argv[])
{
  if (argc < 1) {
    abort();
  }

  if (argc != 5) {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  string src_path = argv[1];
  string dst_ip = argv[2];
  uint16_t dst_port = narrow_cast<uint16_t>(stoi(argv[3]));
  string dst_path = argv[4];

  TCPSocket socket;
  socket.connect({dst_ip, dst_port});
  cerr << "Connected to " << socket.peer_address().str() << endl;

  // TODO
  socket.write("dummy data");

  return EXIT_SUCCESS;
}
