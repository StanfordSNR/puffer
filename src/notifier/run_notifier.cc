#include <iostream>
#include <vector>
#include <string>
#include <stdexcept>
#include <cassert>
#include <sys/inotify.h>

#include "path.hh"
#include "system_runner.hh"
#include "child_process.hh"
#include "notifier.hh"

using namespace std;

void run_program(const string & program, const vector<string> & args)
{
  cerr << "Running: " << command_str(args, {}) << endl;

  ChildProcess proc(args[0],
    [&]() {
      return ezexec(program, args, {}, true);
    }
  );
}

void print_usage(const string & program_name)
{
  cerr << "Usage: " << program_name << " <SRCDIR> <DSTDIR> <PROGRAM>" << endl
       << endl
       << "<SRCDIR>     source directory" << endl
       << "<DSTDIR>     destination directory" << endl
       << "<PROGRAM>    program to run " << endl;
}

int main(int argc, char * argv[])
{
  if (argc < 1) {
    abort();
  }

  if (argc != 4) {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  string src_dir{argv[1]}, dst_dir{argv[2]}, program{argv[3]};

  Notifier notifier;

  notifier.add_watch(src_dir, IN_MOVED_TO,
    [&](const inotify_event & event, const string & path) {
      if (not (event.mask & IN_MOVED_TO)) {
        /* only interested in IN_MOVED_TO */
        return;
      }

      if (event.mask & IN_ISDIR) {
        /* ignore directories moved into source directory */
        return;
      }

      /* here: a regular file must have been moved to srcdir */
      assert(src_dir == path);

      if (event.len == 0) {
        throw runtime_error("returned event should contain a new filename");
      }

      roost::path src_path = roost::path(src_dir) / roost::path(event.name);
      vector<string> args{program, src_path.string(), dst_dir};

      run_program(program, args);
    }
  );

  notifier.loop();

  return EXIT_SUCCESS;
}
