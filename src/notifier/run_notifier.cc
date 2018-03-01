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
  "Usage: " << program_name << " <src_dir> <src_ext> [--check <dst_dir> <dst_ext>] <program> [prog args]\n\n"
  "<src_dir>                      source directory\n"
  "<src_ext>                      extension of files in source directory\n"
  "[--check <dst_dir> <dst_ext>]  check that an output file was created in the directory\n"
  "<program>                      program to run after a new file <src_filename>\n"
  "                               is moved into <src_dir>. The program must take\n"
  "                               at least one arg: <src_filename>. If --check is\n"
  "                               must take a second arg: <dst_filename>\n"
  "[prog args]                    other args to pass to the program"
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
                         const optional<string> & dst_file_opt,
                         const vector<string> & prog_args)
{
  vector<string> args{program, src_file};
  if (dst_file_opt.has_value()) {
    args.push_back(dst_file_opt.value());
  }
  args.insert(args.end(), prog_args.begin(), prog_args.end());
  cerr << "$ " + command_str(args, {}) + "\n";

  return ChildProcess( args[0],
    [&]()
    {
      return ezexec( program, args, {}, true, true );
    }
  );
}

class ParallelNotifier
{
public:
  ParallelNotifier(const string & src_dir,
                   const string & src_ext,
                   const optional<string> & dst_dir_opt,
                   const optional<string> & dst_ext_opt,
                   const string & program,
                   const vector<string> & prog_args);

  void process_existing_files();

  void loop();

private:
  string src_dir_;
  string src_ext_;
  optional<string> dst_dir_opt_;
  optional<string> dst_ext_opt_;
  string program_;
  vector<string> prog_args_;

  SignalMask signals_;
  SignalFD signal_fd_;

  Poller poller_;
  Notifier notifier_;

  unordered_map<pid_t, ChildProcess> child_processes_;
  unordered_map<pid_t, string> src_file_prefixes_;

  void add_child(ChildProcess & child, const string & src_file_prefix);

  void check_and_move_output(const ChildProcess & child);

  optional<string> get_dst_file(const string & src_file_prefix);

  optional<string> get_tmp_dst_file(const string & src_file_prefix);

  Result handle_signal(const signalfd_siginfo &);
};

optional<string> ParallelNotifier::get_tmp_dst_file(const string & src_file_prefix) {
  optional<string> tmp_dst_file;
  if (dst_dir_opt_.has_value()) {
    tmp_dst_file = (fs::path(dst_dir_opt_.value()) / fs::path(src_file_prefix + ".tmp")).string();
  }
  return tmp_dst_file;
}

optional<string> ParallelNotifier::get_dst_file(const string & src_file_prefix) {
  optional<string> dst_file;
  if (dst_dir_opt_.has_value()) {
    assert( dst_ext_opt_.has_value() );
    dst_file = (fs::path(dst_dir_opt_.value()) /
                fs::path(src_file_prefix + "." + dst_ext_opt_.value())).string();
  }
  return dst_file;
}

ParallelNotifier::ParallelNotifier(const string & src_dir,
                                   const string & src_ext,
                                   const optional<string> & dst_dir_opt,
                                   const optional<string> & dst_ext_opt,
                                   const string & program,
                                   const vector<string> & prog_args)
  : src_dir_(src_dir), src_ext_(src_ext),
    dst_dir_opt_(dst_dir_opt), dst_ext_opt_(dst_ext_opt),
    program_(program), prog_args_(prog_args),
    signals_({ SIGCHLD, SIGABRT, SIGHUP, SIGINT, SIGQUIT, SIGTERM }),
    signal_fd_(signals_),
    poller_(), notifier_(poller_),
    child_processes_(), src_file_prefixes_()
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

      string src_basename = event.name;
      auto src_split = split_filename(src_basename);
      string src_file_prefix = src_split.first;

      /* Ignore files that do not match source ext */
      if (src_split.second != src_ext_) {
        return;
      }

      string src_file = (fs::path(src_dir_) / fs::path(src_basename)).string();

      optional<string> tmp_dst_file_opt = get_tmp_dst_file(src_file_prefix);
      auto child_process = run_program(program_, src_file, tmp_dst_file_opt,
                                       prog_args_);
      add_child(child_process, src_file_prefix);
    }
  );

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

void ParallelNotifier::process_existing_files()
{
  unordered_set<string> dst_fileset;

  if (dst_dir_opt_.has_value()) {
    assert( dst_ext_opt_.has_value() );

    /* create a set containing the basename of files in dst_dir */
    string dst_dir = dst_dir_opt_.value();
    vector<string> dst_filenames = get_file_listing(dst_dir);
    for (const string & dst_filename : dst_filenames) {
      auto dst_split = split_filename(dst_filename);
      if (dst_split.second == dst_ext_opt_.value()) {
        dst_fileset.insert(dst_split.first);
      }
    }
  }

  for (const string & src_filename : get_file_listing(src_dir_)) {
    auto src_split = split_filename(src_filename);
    string src_file_prefix = src_split.first;
    if (src_split.second == src_ext_) {
      /* Process src_filename only if no file in dst_dir has the same prefix.
       * If no dst_dir is given, then dst_fileset is empty so all src files are
       * processed */
      if (dst_fileset.find(src_file_prefix) == dst_fileset.end()) {
        string src_file = (fs::path(src_dir_) / fs::path(src_filename)).string();
        optional<string> tmp_dst_file_opt = get_tmp_dst_file(src_file_prefix);
        auto child_process = run_program(program_, src_file, tmp_dst_file_opt,
                                         prog_args_);
        add_child(child_process, src_file_prefix);
      }
    }
  }
}

void ParallelNotifier::loop()
{
  for (;;) {
    poller_.poll(-1);
  }
}

void ParallelNotifier::add_child(ChildProcess & child,
                                 const string & src_file_prefix)
{
  pid_t child_pid = child.pid();
  src_file_prefixes_[child_pid] = src_file_prefix;
  child_processes_.emplace(make_pair(child_pid, move(child)));
}

void ParallelNotifier::check_and_move_output(const ChildProcess & child)
{
  assert( child.terminated() );

  pid_t child_pid = child.pid();
  string src_file_prefix = src_file_prefixes_[child_pid];
  src_file_prefixes_.erase(child_pid);

  optional<string> tmp_dst_file_opt = get_tmp_dst_file(src_file_prefix);
  if (tmp_dst_file_opt.has_value()) {
    string dst_file = get_dst_file(src_file_prefix).value();

    /* Rename from the temporary file to final file */
    fs::rename(tmp_dst_file_opt.value(), dst_file);
    if (!fs::exists(dst_file)) {
      throw runtime_error("program didn't write correct file to dst dir");
    }
  }
}

Result ParallelNotifier::handle_signal(const signalfd_siginfo & sig)
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
            child_processes_.clear();
            throw runtime_error("ParallelNotifier: PID " + to_string(it->first)
                                + " exits abnormally");
          }

          /* Verify that the correct output has been written */
          check_and_move_output(child);

          it = child_processes_.erase(it);
        } else {
          if (not child.running()) {
            child_processes_.clear();
            throw runtime_error("ParallelNotifier: PID " + to_string(it->first)
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
    child_processes_.clear();
    throw runtime_error("ParallelNotifier: interrupted by signal " +
                        to_string(sig.ssi_signo));

  default:
    child_processes_.clear();
    throw runtime_error("ParallelNotifier: unknown signal " +
                        to_string(sig.ssi_signo));
  }

  return ResultType::Continue;
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

  /* convert src & dst dir to absolute paths */
  int arg_idx = 1;
  string src_dir = fs::canonical(argv[arg_idx++]).string();
  string src_ext = argv[arg_idx++];
  optional<string> dst_dir_opt, dst_ext_opt;

  if (string(argv[arg_idx]) == "--check") {
    dst_dir_opt = fs::canonical(argv[arg_idx + 1]).string();
    dst_ext_opt = argv[arg_idx + 2];
    arg_idx += 3;
  }

  string program = argv[arg_idx++];

  vector<string> prog_args;
  for (int i = arg_idx; i < argc; ++i) {
    prog_args.emplace_back(argv[i]);
  }

  ParallelNotifier notifier(src_dir, src_ext, dst_dir_opt, dst_ext_opt,
                            program, prog_args);
  notifier.process_existing_files();
  notifier.loop();

  return EXIT_SUCCESS;
}
