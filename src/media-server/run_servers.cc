#include <iostream>
#include <string>

#include "filesystem.hh"
#include "path.hh"
#include "child_process.hh"

using namespace std;

void print_usage(const string & program_name)
{
  cerr <<
  "Usage: " << program_name << " <YAML configuration> <number of servers>"
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

  int num_servers = stoi(argv[2]);

  /* get the path of ws_media_server */
  auto media_server_dir = fs::canonical(fs::path(
                          roost::readlink("/proc/self/exe")).parent_path());
  auto ws_media_server = media_server_dir / "ws_media_server";

  ProcessManager proc_manager;

  /* run multiple instances of ws_media_server */
  for (int i = 1; i <= num_servers; i++) {
    vector<string> args { ws_media_server, argv[1] };
    proc_manager.run_as_child(ws_media_server, args);
  }

  return proc_manager.wait();
}
