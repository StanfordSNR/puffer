#include <getopt.h>
#include <unistd.h>
#include <sys/stat.h>
#include <iostream>
#include <string>
#include <regex>
#include <chrono>
#include <system_error>

#include "filesystem.hh"
#include "exception.hh"

using namespace std;

void print_usage(const string & program_name)
{
  cerr <<
  "Usage: " << program_name << " [options] <dir>\n\n"
  "<dir>    directory to remove files\n\n"
  "Options:\n"
  "-r                         recursively\n"
  "-p, --pattern <pattern>    remove files whose names matching <pattern>\n"
  "-t, --time <time>          remove files that have not been accessed for <time> seconds"
  << endl;
}

void remove_file(const string & path,
                 const string & pattern, const int stale_time_sec)
{
  error_code ec;
  if (not fs::is_regular_file(path, ec) or ec) {
    /* only interested in regular files */
    return;
  }

  if (pattern.size()) {
    string filename = fs::path(path).filename().string();
    if (not regex_match(filename, regex(pattern))) {
      /* filename does not match pattern */
      return;
    }
  }

  if (stale_time_sec != -1) {
    struct stat file_stat;
    if (stat(path.c_str(), &file_stat)) {
      cerr << "Warning: failed to run stat on " << path << endl;
      return;
    }

    timespec file_time = file_stat.st_atim;
    auto last_chrono = chrono::seconds(file_time.tv_sec);
    auto last = chrono::system_clock::time_point(last_chrono);

    auto now = chrono::system_clock::now();
    auto elapsed_sec = chrono::duration_cast<chrono::seconds>(now - last);

    if (elapsed_sec.count() < stale_time_sec) {
      /* file is not stale yet */
      return;
    }
  }

  /* remove the file if it passes all filters */
  if (not fs::remove(path, ec) or ec) {
    cerr << "Warning: file " << path << " cannot be removed" << endl;
  }
}

void clean_files(const string & working_dir, const bool recursive,
                 const string & pattern, const int stale_time_sec)
{
  if (recursive) {
    for (const auto & entry : fs::recursive_directory_iterator(working_dir)) {
      remove_file(entry.path().string(), pattern, stale_time_sec);
    }
  } else {
    for (const auto & entry : fs::directory_iterator(working_dir)) {
      remove_file(entry.path().string(), pattern, stale_time_sec);
    }
  }
}

int main(int argc, char * argv[])
{
  if (argc < 1) {
    abort();
  }

  string working_dir, pattern, stale_time;
  bool recursive = false;

  const option cmd_line_opts[] = {
    {"pattern", required_argument, nullptr, 'p'},
    {"time",    required_argument, nullptr, 't'},
    { nullptr,  0,                 nullptr,  0 },
  };

  while (true) {
    const int opt = getopt_long(argc, argv, "p:t:r", cmd_line_opts, nullptr);
    if (opt == -1) {
      break;
    }

    switch (opt) {
    case 'p':
      pattern = optarg;
      break;
    case 't':
      stale_time = optarg;
      break;
    case 'r':
      recursive = true;
      break;
    default:
      print_usage(argv[0]);
      return EXIT_FAILURE;
    }
  }

  if (optind != argc - 1) {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  working_dir = argv[optind];

  error_code ec;
  if (not fs::is_directory(working_dir, ec) or ec) {
    cerr << "directory " << working_dir << " does not exist" << endl;
    return EXIT_FAILURE;
  }

  int stale_time_sec = -1;
  if (stale_time.size()) {
    stale_time_sec = stoi(stale_time);
    if (stale_time_sec <= 0) {
      cerr << "--time should be greater than 0" << endl;
      return EXIT_FAILURE;
    }
  }

  if (not (pattern.size() or stale_time_sec > 0)) {
    /* do nothing if no filter is specified */
    cerr << "error: neither --pattern nor --time is specified" << endl;
    return EXIT_FAILURE;
  }

  clean_files(working_dir, recursive, pattern, stale_time_sec);

  return EXIT_SUCCESS;
}
