#include <sys/inotify.h>
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>
#include <string>
#include <stdexcept>

#include "path.hh"
#include "system_runner.hh"
#include "exception.hh"
#include "notifier.hh"

using namespace std;

void print_usage(const string & program_name)
{
  cerr <<
  "Usage: " << program_name << " <dir>... -exec <program> <prog args>\n\n"
  "<dir>        directory to monitor IN_MOVED_TO events of regular files\n"
  "<program>    program to run after a new file <filename> is moved into\n"
  "             one of the <dir>. <prog args> must contain a {} that will be\n"
  "             replaced by <dir>/<filename>."
  << endl;
}

int main(int argc, char * argv[])
{
  if (argc < 1) {
    abort();
  }

  vector<string> monitored_dirs;
  bool after_exec = false;

  string program;
  vector<string> prog_args;
  int braces_idx = -1;

  for (int i = 1; i < argc; ++i) {
    if (!strcmp(argv[i], "-exec")) {
      after_exec = true;
      continue;
    }

    if (not after_exec) {
      string dir_abs_path = roost::canonical(argv[i]).string();
      monitored_dirs.emplace_back(move(dir_abs_path));
    } else {
      if (program.empty()) {
        program = argv[i];
      } else {
        prog_args.emplace_back(argv[i]);

        if (!strcmp(argv[i], "{}")) {
          braces_idx = prog_args.size();
        }
      }
    }
  }

  if (not after_exec or program.empty() or braces_idx == -1) {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  Notifier notifier;

  for (const auto & dir : monitored_dirs) {
    notifier.add_watch(dir, IN_MOVED_TO,
      [&](const inotify_event & event, const string & path) {
        if (not (event.mask & IN_MOVED_TO)) {
          /* only interested in event IN_MOVED_TO */
          return;
        }

        if (event.mask & IN_ISDIR) {
          /* ignore directories moved into source directory */
          return;
        }

        assert(dir == path);

        if (event.len == 0) {
          throw runtime_error("returned event should contain a new filename");
        }

        string filepath = roost::join(dir, event.name);

        vector<string> args{program};
        args.insert(args.end(), prog_args.begin(), prog_args.end());
        args[braces_idx] = filepath;

        cerr << "$ " << command_str(args, {}) << endl;

        run(program, args, {}, true, true);
      }
    );
  }

  notifier.loop();

  return EXIT_SUCCESS;
}
