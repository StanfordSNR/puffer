#include "notifier.hh"

#include <sys/inotify.h>
#include <iostream>
#include <unordered_set>
#include "filesystem.hh"
#include "system_runner.hh"

using namespace std;
using namespace PollerShortNames;

void print_usage(const string & prog)
{
  cerr <<
  "Usage: " << prog << " <src_dir> <src_ext> [--check <dst_dir> <dst_ext>]\n"
  "       [--tmp <tmp_dir>] --exec <program> [program args]\n\n"
  "<src_dir>           source directory\n"
  "<src_ext>           extension of files in <src_dir> to watch\n"
  "[--check <dst_dir> <dst_ext>]\n"
  "                    make sure an output file with extension <dst_ext>\n"
  "                    appears in <dst_dir> eventually\n"
  "[--tmp <tmp_dir>]   temporary directory to use whenever it is needed\n"
  "--exec <program>    program to run after a new file <src_filepath> is\n"
  "                    moved into <src_dir>. The program must take at least\n"
  "                    one argument: <src_filepath>, and must take a second\n"
  "                    argument <dst_filepath> if --check is present\n"
  "[program args]      other args to pass to the program; all the args after\n"
  "                    <program> will be considered as [program args]"
  << endl;
}

Notifier::Notifier(const string & src_dir,
                   const string & src_ext,
                   const optional<string> & dst_dir_opt,
                   const optional<string> & dst_ext_opt,
                   const optional<string> & tmp_dir_opt,
                   const string & program,
                   const vector<string> & prog_args)
  : src_dir_(src_dir), src_ext_(src_ext),
    check_mode_(false), dst_dir_(), dst_ext_(),
    tmp_dir_(), program_(program), prog_args_(prog_args),
    process_manager_(), inotify_(process_manager_.poller()),
    prefixes_()
{
  /* check mode */
  if (dst_dir_opt and dst_ext_opt) {
    check_mode_ = true;
    dst_dir_ = *dst_dir_opt;
    dst_ext_ = *dst_ext_opt;
  }

  /* use default temporary directory if not specified */
  if (tmp_dir_opt) {
    tmp_dir_ = *tmp_dir_opt;
  } else {
    tmp_dir_ = fs::temp_directory_path();
  }

  /* watch moved-in files and run programs as child processes */
  inotify_.add_watch(src_dir_, IN_MOVED_TO,
    [this](const inotify_event & event, const string & path) {
      /* only interested in regular files that are moved into the directory */
      if (not (event.mask & IN_MOVED_TO) or (event.mask & IN_ISDIR)) {
        return;
      }

      assert(src_dir_ == path);
      assert(event.len != 0);

      string filename = event.name;

      /* ignore files that do not match source extension */
      if (src_ext_ != "." and fs::path(filename).extension() != src_ext_) {
        return;
      }

      run_as_child(filename);
    }
  );
}

inline string Notifier::get_src_path(const string & prefix)
{
  return fs::path(src_dir_) / (prefix + src_ext_);
}

inline string Notifier::get_dst_path(const string & prefix)
{
  assert(check_mode_);
  return fs::path(dst_dir_) / (prefix + dst_ext_);
}

inline string Notifier::get_tmp_path(const string & prefix)
{
  assert(check_mode_);
  return fs::path(tmp_dir_) / (prefix + dst_ext_);
}

void Notifier::run_as_child(const string & filename)
{
  string prefix = fs::path(filename).stem();

  /* create arguments passed to program_ */
  vector<string> args { program_ };

  if (src_ext_ == ".") {
    /* keep the original extension if interested in any file */
    args.emplace_back(fs::path(src_dir_) / filename);
  } else {
    args.emplace_back(get_src_path(prefix));
  }

  if (check_mode_) {
    /* make program output to a tmp path and move to the dst path later */
    args.emplace_back(get_tmp_path(prefix));
  }

  args.insert(args.end(), prog_args_.begin(), prog_args_.end());

  /* run program_ as a child */
  if (check_mode_) {
    pid_t pid = process_manager_.run_as_child(program_, args,
      [this](const pid_t & pid) {
        /* verify that the correct output has been written */
        assert(check_mode_);

        string prefix = prefixes_[pid];

        /* throw an exception if get_tmp_path(prefix) does not exist */
        fs::rename(get_tmp_path(prefix), get_dst_path(prefix));

        prefixes_.erase(pid);
      }
    );

    prefixes_.emplace(pid, prefix);
  } else {
    process_manager_.run_as_child(program_, args);
  }
}

void Notifier::process_existing_files()
{
  unordered_set<string> dst_prefixes;

  if (check_mode_) {
    for (const auto & dst : fs::directory_iterator(dst_dir_)) {
      if (dst.path().extension() == dst_ext_) {
        dst_prefixes.insert(dst.path().stem());
      }
    }
  }

  for (const auto & src : fs::directory_iterator(src_dir_)) {
    const auto & src_path = src.path();
    if (src_path.extension() == src_ext_) {
      string filename = src_path.filename();
      string prefix = src_path.stem();

      if (check_mode_) {
        /* in check mode only process files with no outputs in dst_dir */
        if (dst_prefixes.find(prefix) == dst_prefixes.end()) {
          run_as_child(filename);
        }
      } else {
        /* otherwise process every file in src_dir with src_ext */
        run_as_child(filename);
      }
    }
  }
}

int Notifier::loop()
{
  return process_manager_.loop();
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

  /* parse arguments */
  int arg_idx = 1;
  string src_dir = argv[arg_idx++];
  string src_ext = argv[arg_idx++];

  optional<string> dst_dir_opt, dst_ext_opt;
  optional<string> tmp_dir_opt;

  for (;;) {
    if (arg_idx >= argc) {
      print_usage(argv[0]);
      cerr << "Error: --exec is required" << endl;
      return EXIT_FAILURE;
    }

    string opt_arg = string(argv[arg_idx++]);

    if (opt_arg == "--check") {
      dst_dir_opt = argv[arg_idx++];
      dst_ext_opt = argv[arg_idx++];
    } else if (opt_arg == "--tmp") {
      tmp_dir_opt = argv[arg_idx++];
    } else if (opt_arg == "--exec") {
      break;
    }
  }

  /* interested in any file extension */
  if (src_ext == ".") {
    /* --check is not allowed */
    if (dst_dir_opt or dst_ext_opt) {
      cerr << "Error: --check is not allowed when src_ext is ." << endl;
      return EXIT_FAILURE;
    }
  }

  /* the remaining arguments should be <program> [program args] */
  string program = argv[arg_idx++];

  vector<string> prog_args;
  for (int i = arg_idx; i < argc; ++i) {
    prog_args.emplace_back(argv[i]);
  }

  Notifier notifier(src_dir, src_ext, dst_dir_opt, dst_ext_opt,
                    tmp_dir_opt, program, prog_args);
  notifier.process_existing_files();
  return notifier.loop();
}
