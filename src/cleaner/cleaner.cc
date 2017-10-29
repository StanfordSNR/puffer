#include <iostream>
#include <string>
#include <regex>
#include <chrono>
#include <getopt.h>
#include <sys/stat.h>

#include "path.hh"
#include "strict_conversions.hh"
#include "exception.hh"

using namespace std;

void print_usage(const string & program_name)
{
  cerr <<
  "Usage: " << program_name << " [options] <dir>\n\n"
  "<dir>    directory to remove files (remove nothing without args below)\n\n"
  "Options:\n"
  "-p, --pattern <pattern>    remove files whose names matching <pattern>\n"
  "-t, --time <time>          remove files that have not been accessed\n"
  "                           for <time> seconds"
  << endl;
}

void clean_files(const string & working_dir, const string & pattern,
                 const string & stale_time)
{
  if (pattern.empty() and stale_time.empty()) {
    /* do nothing if no filter is specified */
    return;
  }

  regex pattern_regex;
  if (not pattern.empty()) {
    pattern_regex = regex(pattern);
  }

  long int stale_time_sec = 0;
  if (not stale_time.empty()) {
    stale_time_sec = strict_atoi(stale_time);
  }

  vector<string> filenames = roost::get_directory_listing(working_dir);

  for (const auto & filename : filenames) {
    string fullpath = roost::join(working_dir, filename);

    if (not pattern.empty() and not regex_match(filename, pattern_regex)) {
      /* filename does not match pattern */
      continue;
    }

    if (not stale_time.empty()) {
      struct stat file_stat;
      CheckSystemCall("stat", stat(fullpath.c_str(), &file_stat));

      timespec file_time = file_stat.st_atim;
      auto last_chrono = chrono::seconds(file_time.tv_sec) +
                         chrono::nanoseconds(file_time.tv_nsec);
      auto last = chrono::system_clock::time_point(last_chrono);

      auto now = chrono::system_clock::now();
      auto elapsed = chrono::duration_cast<chrono::milliseconds>(now - last);

      if (elapsed.count() < 1000 * stale_time_sec) {
        /* file is not stale yet */
        continue;
      }
    }

    /* remove the file if it passes all filters */
    if (not roost::remove(fullpath)) {
      cerr << "Warning: unable to remove file " << fullpath << endl;
    }
  }
}

int main(int argc, char * *argv)
{
  if (argc < 1) {
    abort();
  }

  string working_dir, pattern, stale_time;

  const option cmd_line_opts[] = {
    {"pattern", required_argument, nullptr, 'p'},
    {"time",    required_argument, nullptr, 't'},
    { nullptr,  0,                 nullptr,  0 },
  };

  while (true) {
    const int opt = getopt_long(argc, argv, "p:t:", cmd_line_opts, nullptr);
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
  clean_files(working_dir, pattern, stale_time);

  return EXIT_SUCCESS;
}
