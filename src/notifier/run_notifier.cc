#include <sys/inotify.h>
#include <cassert>
#include <iostream>
#include <vector>
#include <string>
#include <stdexcept>
#include <set>

#include "path.hh"
#include "system_runner.hh"
#include "tokenize.hh"
#include "exception.hh"
#include "notifier.hh"

using namespace std;

void print_usage(const string & program_name)
{
  cerr <<
  "Usage: " << program_name << " <src_dir> <dst_dir> <program> [prog args]\n\n"
  "<src_dir>    source directory\n"
  "<dst_dir>    destination directory\n"
  "<program>    program to run after a new file <src_filename>\n"
  "             is moved into <src_dir>. The program must take\n"
  "             two args <src_filename> and <dst_dir>.\n"
  "[prog args]  other args to pass to the program"
  << endl;
}

void run_program(const string & program,
                 const string & src_file,
                 const string & dst_dir,
                 const vector<string> & prog_args)
{
  vector<string> args{program, src_file, dst_dir};
  args.insert(args.end(), prog_args.begin(), prog_args.end());
  cerr << "$ " << command_str(args, {}) << endl;

  run(program, args, {}, true, true);

  /* check if program wrote to dst_dir correctly */
  string src_filename = roost::rbasename(src_file).string();
  string src_filename_prefix = split_filename(src_filename).first;

  vector<string> dst_filenames = roost::get_directory_listing(dst_dir);

  bool success = false;
  for (const string & dst_filename : dst_filenames) {
    if (src_filename_prefix == split_filename(dst_filename).first) {
      success = true;
      break;
    }
  }

  if (not success) {
    throw runtime_error("program didn't write correct file to dst dir");
  }
}

void process_existing_files(const string & program,
                            const string & src_dir,
                            const string & dst_dir,
                            const vector<string> & prog_args)
{
  /* create a set containing the basename of files in dst_dir */
  vector<string> dst_filenames = roost::get_directory_listing(dst_dir);
  set<string> dst_fileset;

  for (const string & dst_filename : dst_filenames) {
    dst_fileset.insert(split_filename(dst_filename).first);
  }

  vector<string> src_filenames = roost::get_directory_listing(src_dir);

  for (const string & src_filename : src_filenames) {
    string src_filename_prefix = split_filename(src_filename).first;

    /* process src_filename only if no file in dst_dir has the same prefix */
    if (dst_fileset.find(src_filename_prefix) == dst_fileset.end()) {
      string src_file = roost::join(src_dir, src_filename);
      run_program(program, src_file, dst_dir, prog_args);
    }
  }
}

int main(int argc, char * argv[])
{
  if (argc < 1) {
    abort();
  }

  if (argc < 4) {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  string src_dir{argv[1]}, dst_dir{argv[2]}, program{argv[3]};

  vector<string> prog_args;
  for (int i = 4; i < argc; ++i) {
    prog_args.emplace_back(argv[i]);
  }

  /* convert src & dst dir to absolute paths as they'll be passed to program */
  src_dir = roost::canonical(src_dir).string();
  dst_dir = roost::canonical(dst_dir).string();

  Notifier notifier;

  notifier.add_watch(src_dir, IN_MOVED_TO,
    [&](const inotify_event & event, const string & path) {
      if (not (event.mask & IN_MOVED_TO)) {
        /* only interested in event IN_MOVED_TO */
        return;
      }

      if (event.mask & IN_ISDIR) {
        /* ignore directories moved into source directory */
        return;
      }

      assert(src_dir == path);

      if (event.len == 0) {
        throw runtime_error("returned event should contain a new filename");
      }

      string src_file = roost::join(src_dir, event.name);
      run_program(program, src_file, dst_dir, prog_args);
    }
  );

  /* process pre-existing files in srcdir after Notifier starts watching
   * so that no new files will be missed */
  process_existing_files(program, src_dir, dst_dir, prog_args);

  notifier.loop();

  return EXIT_SUCCESS;
}
