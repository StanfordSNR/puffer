#include <sys/inotify.h>
#include <cassert>
#include <iostream>
#include <map>
#include <vector>
#include <optional>
#include <string>
#include <stdexcept>
#include <system_error>
#include <unordered_set>

#include "filesystem.hh"
#include "child_process.hh"
#include "system_runner.hh"
#include "tokenize.hh"
#include "exception.hh"
#include "notifier.hh"
#include "signalfd.hh"
#include "poller.hh"

using namespace std;
using namespace PollerShortNames;

void print_usage(const string & program_name)
{
  cerr <<
  "Usage: " << program_name << " <src_dir> [--check <dst_dir>] <program> [prog args]\n\n"
  "<src_dir>            source directory\n"
  "[--check <dst_dir>]  check that an output file was created in the directory\n"
  "<program>            program to run after a new file <src_filename>\n"
  "                     is moved into <src_dir>. The program must take\n"
  "                     at least one arg: <src_filename>.\n"
  "[prog args]          other args to pass to the program"
  << endl;
}

/* filesystem based file listing */
vector<string> get_file_listing(const string & dst_dir)
{
  error_code ec;
  if (not fs::is_directory(dst_dir, ec) or ec) {
    throw runtime_error(dst_dir + " is not a directory");
  }

  vector<string> result;
  for (const auto & path : fs::directory_iterator(dst_dir)) {
    if (fs::is_regular_file(path, ec) and not ec) {
      result.push_back(path.path().filename().string());
    }
  }

  return result;
}

ChildProcess run_program(const string & program,
                         const string & src_file,
                         const vector<string> & prog_args)
{
  vector<string> args{program, src_file};
  args.insert(args.end(), prog_args.begin(), prog_args.end());
  cerr << "$ " << command_str(args, {}) << endl;

  return ChildProcess( args[0],
    [=]()
    {
      return ezexec( program, args, {}, true, true );
    }
  );
}

class ParallelNotifier
{
public:
  ParallelNotifier(const string & src_dir,
                   const optional<string> & dst_dir_opt,
                   const string & program,
                   const vector<string> & prog_args);

  void process_existing_files();

  void loop();

private:
  string src_dir_;
  optional<string> dst_dir_opt_;
  string program_;
  vector<string> prog_args_;

  SignalMask signals_;
  SignalFD signal_fd_;

  Poller poller_;
  Notifier notifier_;

  unordered_map<pid_t, ChildProcess> child_processes_;
  unordered_map<pid_t, string> src_files_;

  void add_child(ChildProcess & child, const string & src_file);

  void check_output(const ChildProcess & child);

  Poller::Action::Result handle_signal(const signalfd_siginfo &);
};

ParallelNotifier::ParallelNotifier(const string & src_dir,
                                   const optional<string> & dst_dir_opt,
                                   const string & program,
                                   const vector<string> & prog_args)
  : src_dir_(src_dir), dst_dir_opt_(dst_dir_opt),
    program_(program), prog_args_(prog_args),
    signals_({ SIGCHLD, SIGCONT, SIGHUP, SIGTERM, SIGQUIT, SIGINT }),
    signal_fd_(signals_),
    poller_(), notifier_(poller_),
    child_processes_(), src_files_()
{
  notifier_.add_watch(src_dir_, IN_MOVED_TO,
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

      string src_file = (fs::path(src_dir_) / fs::path(event.name)).string();
      auto child_process = run_program(program_, src_file, prog_args_);
      add_child(child_process, src_file);
    }
  );

  signals_.set_as_mask();
  poller_.add_action(
    Poller::Action{
      signal_fd_.fd(), Direction::In,
      [&]() { return handle_signal(signal_fd_.read_signal()); },
      [&]() { return child_processes_.size() > 0; }
    }
  );
}

void ParallelNotifier::process_existing_files()
{
  unordered_set<string> dst_fileset;

  if (dst_dir_opt_.has_value()) {
    /* create a set containing the basename of files in dst_dir */
    string dst_dir = dst_dir_opt_.value();
    vector<string> dst_filenames = get_file_listing(dst_dir);
    for (const string & dst_filename : dst_filenames) {
      dst_fileset.insert(split_filename(dst_filename).first);
    }
  }

  vector<string> src_filenames = get_file_listing(src_dir_);

  for (const string & src_filename : src_filenames) {
    string src_filename_prefix = split_filename(src_filename).first;

    /* Process src_filename only if no file in dst_dir has the same prefix.
     * If no dst_dir is given, then dst_fileset is empty so all src files are
     * processed */
    if (dst_fileset.find(src_filename_prefix) == dst_fileset.end()) {
      string src_file = (fs::path(src_dir_) / fs::path(src_filename)).string();
      auto child_process = run_program(program_, src_file, prog_args_);
      add_child(child_process, src_file);
    }
  }
}

void ParallelNotifier::loop()
{
  while (true) {
    poller_.poll(-1);
  }
}

void ParallelNotifier::add_child(ChildProcess & child, const string & src_file)
{
  pid_t child_pid = child.pid();
  src_files_[child_pid] = src_file;
  child_processes_.emplace(make_pair(child_pid, move(child)));
}

void ParallelNotifier::check_output(const ChildProcess & child)
{
  assert( child.terminated() );
  assert( dst_dir_opt_.has_value() );

  string dst_dir = dst_dir_opt_.value();
  pid_t child_pid = child.pid();
  string src_file = src_files_[child_pid];
  src_files_.erase(child_pid);

  /* check if program wrote to dst_dir correctly */
  string src_filename = fs::path(src_file).filename().string();
  string src_filename_prefix = split_filename(src_filename).first;

  vector<string> dst_filenames = get_file_listing(dst_dir);

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

Poller::Action::Result ParallelNotifier::handle_signal(const signalfd_siginfo & sig)
{
  switch (sig.ssi_signo) {
  case SIGCONT:
    for (auto & child : child_processes_) {
      child.second.resume();
    }
    break;

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
            child.throw_exception();
          }

          /* Verify that the correct output has been written */
          if (dst_dir_opt_.has_value()) {
            check_output(child);
          }

          it = child_processes_.erase(it);
        } else {
          if (not child.running()) {
            /* suspend parent too */
            CheckSystemCall("raise", raise(SIGSTOP));
          }
          it++;
        }
      }
    }

    break;

  case SIGHUP:
  case SIGTERM:
  case SIGQUIT:
  case SIGINT:
    throw runtime_error( "interrupted by signal" );

  default:
    throw runtime_error( "unknown signal" );
  }

  return ResultType::Continue;
}

int main(int argc, char * argv[])
{
  if (argc < 1) {
    abort();
  }

  if (argc < 3) {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  /* convert src & dst dir to absolute paths */
  string src_dir = fs::canonical(argv[1]).string();
  optional<string> dst_dir_opt;

  int arg_idx = 2;
  if (string(argv[arg_idx]) == "--check") {
    dst_dir_opt = fs::canonical(argv[arg_idx + 1]).string();;
    arg_idx += 2;
  } else {
    dst_dir_opt = {};
  }

  string program = argv[arg_idx++];

  vector<string> prog_args;
  for (int i = arg_idx; i < argc; ++i) {
    prog_args.emplace_back(argv[i]);
  }

  ParallelNotifier notifier(src_dir, dst_dir_opt, program, prog_args);
  notifier.process_existing_files();
  notifier.loop();

  return EXIT_SUCCESS;
}
