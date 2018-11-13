#include <fcntl.h>

#include <iostream>

#include "strict_conversions.hh"
#include "socket.hh"
#include "file_descriptor.hh"
#include "exception.hh"
#include "filesystem.hh"
#include "file_message.hh"

using namespace std;

void print_usage(const string & program_name)
{
  cerr <<
  "Usage: " << program_name << " SRC-PATH HOST PORT DST-DIR\n\n"
  "Transfer the file at SRC-PATH to DST-DIR on HOST:PORT"
  << endl;
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
  string dst_dir = argv[4];

  TCPSocket socket;
  socket.connect({dst_ip, dst_port});
  cerr << "Connected to " << socket.peer_address().str() << endl;

  /* send dst_path first */
  string dst_path = fs::path(dst_dir) / fs::path(src_path).filename();
  FileMsg metadata(dst_path.size(), dst_path);
  socket.write(metadata.to_string());

  FileDescriptor fd(CheckSystemCall("open (" + src_path + ")",
                    open(src_path.c_str(), O_RDONLY)));
  /* send the file at src_path */
  for (;;) {
    const string data = fd.read();
    if (data.empty()) {  // EOF
      break;
    }

    socket.write(data);
  }

  fd.close();

  cerr << "Delivered file " << src_path << endl;

  return EXIT_SUCCESS;
}
