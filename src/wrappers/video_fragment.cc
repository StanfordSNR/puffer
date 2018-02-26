#include <getopt.h>
#include <iostream>
#include <string>
#include <vector>

#include "exception.hh"
#include "system_runner.hh"
#include "filesystem.hh"
#include "path.hh"  /* readlink */

using namespace std;

void print_usage(const string & program)
{
  cerr <<
  "Usage: " << program << " <input_path> <output_dir> --tmp <tmp_dir>\n"
  "Fragment <input_path> and output to a temporary directory first;\n"
  "then move the output video to <output_dir>\n\n"
  "<input_path>    path to input encoded video\n"
  "<output_dir>    target directory to output fragmented video\n\n"
  "Options:\n"
  "--tmp <tmp_dir>    [optional] replace the default directory suitable for\n"
  "                   temporary files with <tmp_dir>"
  << endl;
}

int main(int argc, char * argv[])
{
  /* parse arguments */
  if (argc < 1) {
    abort();
  }

  string tmp_dir;

  const option cmd_line_opts[] = {
    {"tmp",    required_argument, nullptr, 't'},
    { nullptr, 0,                 nullptr,  0 }
  };

  while (true) {
    const int opt = getopt_long(argc, argv, "t:", cmd_line_opts, nullptr);
    if (opt == -1) {
      break;
    }

    switch (opt) {
    case 't':
      tmp_dir = optarg;
      break;
    default:
      print_usage(argv[0]);
      return EXIT_FAILURE;
    }
  }

  if (optind != argc - 2) {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  /* if <tmp_dir> is not specified, use temp_directory_path() */
  if (tmp_dir.empty()) {
    tmp_dir = fs::temp_directory_path();
  }

  string input_filepath = argv[optind];
  string output_dir = argv[optind + 1];

  /* paths of init segment and media segment */
  string output_filename = fs::path(input_filepath).stem().string() + ".m4s";
  string tmp_filepath = fs::path(tmp_dir) / output_filename;
  string output_filepath = fs::path(output_dir) / output_filename;

  string tmp_initpath = fs::path(tmp_dir) / "init.mp4";
  string output_initpath = fs::path(output_dir) / "init.mp4";

  /* path of mp4_fragment */
  auto exe_dir = fs::path(roost::readlink("/proc/self/exe")).parent_path();
  string mp4_fragment = fs::canonical(exe_dir / "../mp4/mp4_fragment");

  /* output init segment and media segment to tmp_dir first */
  vector<string> args = {
    mp4_fragment, "-i", tmp_initpath, "-m", tmp_filepath, input_filepath };
  cerr << command_str(args, {}) << endl;
  run(mp4_fragment, args, {}, true, true);

  /* move output fragmented videos from tmp_dir to output_dir */
  fs::rename(tmp_filepath, output_filepath);
  fs::rename(tmp_initpath, output_initpath);

  return EXIT_SUCCESS;
}
