#include <getopt.h>
#include <iostream>
#include <string>
#include <vector>

#include "system_runner.hh"
#include "filesystem.hh"
#include "path.hh"  /* readlink */

using namespace std;

void print_usage(const string & program)
{
  cerr <<
  "Usage: " << program << " <input_path> <output_dir> [--tmp <tmp_dir>]\n"
  "Fragment the audio <input_path> and output to <output_dir>\n\n"
  "<input_path>    path of the input encoded audio\n"
  "<output_dir>    target directory to output the fragmented audio\n\n"
  "Options:\n"
  "--tmp <tmp_dir>    replace the default temporary directory with <tmp_dir>"
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
  string media_name = fs::path(input_filepath).stem().string() + ".chk";
  string media_tmp_path = fs::path(tmp_dir) / media_name;
  string media_output_path = fs::path(output_dir) / media_name;

  string init_name = "init.webm";
  string init_tmp_path = fs::path(tmp_dir) / init_name;
  string init_output_path = fs::path(output_dir) / init_name;

  /* path of the mp4_fragment program */
  auto exe_dir = fs::path(roost::readlink("/proc/self/exe")).parent_path();
  string webm_fragment = fs::canonical(exe_dir / "../webm/webm_fragment");

  /* output init segment and media segment to tmp_dir first */
  vector<string> args = {
    webm_fragment, "-i", init_tmp_path, "-m", media_tmp_path, input_filepath };
  cerr << command_str(args, {}) << endl;
  run(webm_fragment, args, {}, true, true);

  /* move the output fragmented audios from tmp_dir to output_dir */
  fs::rename(media_tmp_path, media_output_path);
  fs::rename(init_tmp_path, init_output_path);

  return EXIT_SUCCESS;
}
