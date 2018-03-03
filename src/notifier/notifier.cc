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
    signals_({ SIGCHLD, SIGABRT, SIGHUP, SIGINT, SIGQUIT, SIGTERM }),
    signal_fd_(signals_), poller_(), inotify_(poller_),
    child_processes_(), prefixes_()
{
  /* check mode */
  if (dst_dir_opt.has_value() and dst_ext_opt.has_value()) {
    check_mode_ = true;
    dst_dir_ = dst_dir_opt.value();
    dst_ext_ = dst_ext_opt.value();
  }

  /* use default temporary directory if not specified */
  if (tmp_dir_opt.has_value()) {
    tmp_dir_ = tmp_dir_opt.value();
  } else {
    tmp_dir_ = fs::temp_directory_path();
  }

  /* watch moved-in files and run programs as child processes */
  inotify_.add_watch(src_dir_, IN_MOVED_TO,
    [&](const inotify_event & event, const string & path) {
      if (not (event.mask & IN_MOVED_TO)) {
        /* only interested in event IN_MOVED_TO */
        return;
      }

      if (event.mask & IN_ISDIR) {
        /* ignore directories moved into source directory */
        return;
      }

      assert(src_dir_ == path);

      if (event.len == 0) {
        throw runtime_error("returned event should contain a new filename");
      }

      string filename = event.name;

      /* ignore files that do not match source extension */
      if (fs::path(filename).extension() != src_ext_) {
        return;
      }

      run_as_child(fs::path(filename).stem());
    }
  );

  /* listen on signals from the current process and child processes */
  signals_.set_as_mask();
  poller_.add_action(
    Poller::Action{
      signal_fd_.fd(), Direction::In,
      [&]() {
        return handle_signal(signal_fd_.read_signal());
      }
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

void Notifier::run_as_child(const string & prefix)
{
  /* create arguments passed to program_ */
  vector<string> args { program_, get_src_path(prefix) };
  if (check_mode_) {
    args.emplace_back(get_tmp_path(prefix));
  }

  args.insert(args.end(), prog_args_.begin(), prog_args_.end());
  cerr << "$ " + command_str(args, {}) + "\n";

  /* run program_ as a child */
  auto child = ChildProcess(program_,
    [&]() {
      return ezexec(program_, args);
    }
  );

  pid_t child_pid = child.pid();
  prefixes_.emplace(child_pid, prefix);
  child_processes_.emplace(child_pid, move(child));
}

Result Notifier::handle_signal(const signalfd_siginfo & sig)
{
  switch (sig.ssi_signo) {
  case SIGCHLD:
    if (child_processes_.empty()) {
      throw runtime_error("received SIGCHLD without any managed children");
    }

    for (auto it = child_processes_.begin(); it != child_processes_.end();) {
      ChildProcess & child = it->second;

      if (not child.waitable()) {
        it++;
      } else {
        child.wait(true);

        if (child.terminated()) {
          if (child.exit_status() != 0) {
            throw runtime_error("Notifier: PID " + to_string(it->first)
                                + " exits abnormally");
          }

          /* verify that the correct output has been written */
          pid_t child_pid = child.pid();
          string prefix = prefixes_[child_pid];

          if (check_mode_) {
            /* throw an exception if get_tmp_path(prefix) does not exist */
            fs::rename(get_tmp_path(prefix), get_dst_path(prefix));
          }

          prefixes_.erase(child_pid);
          it = child_processes_.erase(it);
        } else {
          if (not child.running()) {
            throw runtime_error("Notifier: PID " + to_string(it->first)
                                + " is not running");
          }
          it++;
        }
      }
    }

    break;

  case SIGABRT:
  case SIGHUP:
  case SIGINT:
  case SIGQUIT:
  case SIGTERM:
    throw runtime_error("Notifier: interrupted by signal " +
                        to_string(sig.ssi_signo));

  default:
    throw runtime_error("Notifier: unknown signal " +
                        to_string(sig.ssi_signo));
  }

  return ResultType::Continue;
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
    if (src.path().extension() == src_ext_) {
      string prefix = src.path().stem();

      if (check_mode_) {
        /* in check mode only process files with no outputs in dst_dir */
        if (dst_prefixes.find(prefix) == dst_prefixes.end()) {
          run_as_child(prefix);
        }
      } else {
        /* otherwise process every file in src_dir with src_ext */
        run_as_child(prefix);
      }
    }
  }
}

void Notifier::loop()
{
  /* poll forever */
  for (;;) {
    poller_.poll(-1);
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

  /* parse arguments */
  int arg_idx = 1;
  string src_dir = fs::canonical(argv[arg_idx++]).string();
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
      dst_dir_opt = fs::canonical(argv[arg_idx++]).string();
      dst_ext_opt = argv[arg_idx++];
    } else if (opt_arg == "--tmp") {
      tmp_dir_opt = fs::canonical(argv[arg_idx++]).string();
    } else if (opt_arg == "--exec") {
      break;
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
  notifier.loop();

  return EXIT_SUCCESS;
}
