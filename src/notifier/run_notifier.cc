#include <sys/inotify.h>
#include <cassert>
#include <iostream>
#include <list>
#include <vector>
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

class AsyncTask
{
private:
  ChildProcess child_process_;
  string src_file_;
  string dst_dir_;
public:
  AsyncTask( ChildProcess & original,
             const string & src_file,
             const string & dst_dir )
    : child_process_(move(original)),
      src_file_(src_file), 
      dst_dir_(dst_dir) {};

  AsyncTask( AsyncTask && other )
    : child_process_( move(other.child_process_) ), 
      src_file_( other.src_file_ ), 
      dst_dir_( other.dst_dir_ ) {};
  AsyncTask & operator=( AsyncTask && other ) = delete;

  AsyncTask( const AsyncTask & other ) = delete;
  AsyncTask & operator=( const AsyncTask & other ) = delete;

  bool done();

  void check_output();
};

bool AsyncTask::done() {
  return child_process_.terminated();
}

void AsyncTask::check_output() {
  /* check if program wrote to dst_dir correctly */
  string src_filename = fs::path(src_file_).filename().string();
  string src_filename_prefix = split_filename(src_filename).first;

  vector<string> dst_filenames = get_file_listing(dst_dir_);

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

ChildProcess run_program(const string & program,
                         const string & src_file,
                         const string & dst_dir,
                         const vector<string> & prog_args)
{
  vector<string> args{program, src_file, dst_dir};
  args.insert(args.end(), prog_args.begin(), prog_args.end());
  cerr << "$ " << command_str(args, {}) << endl;

  return ChildProcess( args[0], 
    [=]() 
    {
      return ezexec( program, args, {}, true, true );
    }
  );
}

void process_existing_files(const string & program,
                            const string & src_dir,
                            const string & dst_dir,
                            const vector<string> & prog_args,
                            list<AsyncTask> & tasks)
{
  /* create a set containing the basename of files in dst_dir */
  vector<string> dst_filenames = get_file_listing(dst_dir);
  unordered_set<string> dst_fileset;

  for (const string & dst_filename : dst_filenames) {
    dst_fileset.insert(split_filename(dst_filename).first);
  }

  vector<string> src_filenames = get_file_listing(src_dir);

  for (const string & src_filename : src_filenames) {
    string src_filename_prefix = split_filename(src_filename).first;

    /* process src_filename only if no file in dst_dir has the same prefix */
    if (dst_fileset.find(src_filename_prefix) == dst_fileset.end()) {
      string src_file = (fs::path(src_dir) / fs::path(src_filename)).string();
      auto child_process = run_program(program, src_file, dst_dir, prog_args);

      tasks.push_back(AsyncTask(child_process, src_file, dst_dir));
    }
  }
}

void remove_finished_tasks(list<AsyncTask> & tasks) {
  for (auto it = tasks.begin(); it != tasks.end(); ) {
    if (it->done()) {
      it->check_output();
      it = tasks.erase(it);
    } else {
      ++it;
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

  /* convert src & dst dir to absolute paths */
  string src_dir = fs::canonical(argv[1]).string();
  string dst_dir = fs::canonical(argv[2]).string();

  string program = argv[3];

  vector<string> prog_args;
  for (int i = 4; i < argc; ++i) {
    prog_args.emplace_back(argv[i]);
  }

  list<AsyncTask> tasks;

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

      string src_file = (fs::path(src_dir) / fs::path(event.name)).string();
      auto child_process = run_program(program, src_file, dst_dir, prog_args);

      /* add the task to the list of tasks waiting to complete */
      tasks.push_back(AsyncTask(child_process, src_file, dst_dir));
    }
  );

  /* process pre-existing files in srcdir after Notifier starts watching
   * so that no new files will be missed */
  process_existing_files(program, src_dir, dst_dir, prog_args, tasks);

  while (true) {
    notifier.poll(1000);
    remove_finished_tasks(tasks);
  }

  return EXIT_SUCCESS;
}
