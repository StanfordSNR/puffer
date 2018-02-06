#include <stdlib.h>
#include <sys/inotify.h>
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>
#include <unordered_set>
#include <string>
#include <stdexcept>

#include "filesystem.hh"
#include "system_runner.hh"
#include "exception.hh"
#include "notifier.hh"
#include "poller.hh"

using namespace std;

void print_usage(const string & program_name)
{
  cerr <<
  "Usage: " << program_name << " [options] <dir>... -exec <program> <args>\n\n"
  "Options:\n"
  "-q           quit after the first successful run of <program>\n"
  "-a           run <program> only if at least an event occurs in all <dir>\n"
  "<dir>        directory to monitor IN_MOVED_TO events of regular files\n"
  "<program>    program to run after a new file <filename> is moved into\n"
  "             one of the <dir>. <args> must contain a {} that will be\n"
  "             replaced by <dir>/<filename>. When -a exists, {} will be\n"
  "             replaced by all <dir>/<filename> separated by spaces."
  << endl;
}

int main(int argc, char * argv[])
{
  if (argc < 1) {
    abort();
  }

  unordered_set<string> monitored_dirs;
  bool after_exec = false;
  bool quit_after_success = false;

  bool monitor_all = false;
  unordered_set<string> ready_dirs;
  vector<string> ready_filepaths;

  string program;
  vector<string> prog_args;
  int braces_idx = -1;

  for (int i = 1; i < argc; ++i) {
    if (not strcmp(argv[i], "-exec")) {
      after_exec = true;
      continue;
    }

    if (not after_exec) {
      if (not strcmp(argv[i], "-q")) {
        quit_after_success = true;
      } else if (not strcmp(argv[i], "-a")) {
        monitor_all = true;
      } else {
        string dir_abs_path = fs::canonical(argv[i]).string();
        monitored_dirs.insert(dir_abs_path);
      }
    } else {
      if (program.empty()) {
        program = argv[i];
      } else {
        prog_args.emplace_back(argv[i]);

        if (not strcmp(argv[i], "{}")) {
          braces_idx = prog_args.size();
        }
      }
    }
  }

  if (not after_exec or program.empty() or braces_idx == -1) {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  Poller poller;
  Notifier notifier(poller);

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

        string filepath = (fs::path(dir) / fs::path(event.name)).string();
        vector<string> args{program};

        if (monitor_all) {
          ready_dirs.insert(dir);
          ready_filepaths.emplace_back(filepath);

          if (monitored_dirs != ready_dirs) {
            return;
          }

          args.insert(args.end(), prog_args.begin(), prog_args.end());

          auto args_it = args.erase(args.begin() + braces_idx);
          args.insert(args_it, ready_filepaths.begin(), ready_filepaths.end());

          ready_dirs.clear();
          ready_filepaths.clear();
        } else {
          args.insert(args.end(), prog_args.begin(), prog_args.end());
          args[braces_idx] = filepath;
        }

        cerr << "$ " << command_str(args, {}) << endl;
        run(program, args, {}, true, true);

        if (quit_after_success) {
          exit(EXIT_SUCCESS);
        }
      }
    );
  }

  while (true) {
    poller.poll(-1);
  }

  return EXIT_SUCCESS;
}
