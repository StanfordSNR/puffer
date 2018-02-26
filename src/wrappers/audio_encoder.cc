#include <getopt.h>
#include <iostream>
#include <string>
#include <vector>

#include "system_runner.hh"
#include "filesystem.hh"

using namespace std;

void print_usage(const string & program)
{
  cerr <<
  "Usage: " << program << " <input_path> <output_dir> [--tmp <tmp_dir>] "
  "-b <bitrate>\n"
  "Encode the audio <input_path> and output to <output_dir>\n\n"
  "<input_path>    path of the input raw audio\n"
  "<output_dir>    target directory to output the encoded audio\n\n"
  "Options:\n"
  "--tmp <tmp_dir>    replace the default temporary directory with <tmp_dir>\n"
  "-b <bitrate>       bitrate (e.g., 64k)"
  << endl;
}

int main(int argc, char * argv[])
{
  /* parse arguments */
  if (argc < 1) {
    abort();
  }

  string tmp_dir;
  string bitrate;

  const option cmd_line_opts[] = {
    {"tmp",     required_argument, nullptr, 't'},
    {"bitrate", required_argument, nullptr, 'b'},
    { nullptr,  0,                 nullptr,  0 }
  };

  while (true) {
    const int opt = getopt_long(argc, argv, "t:b:", cmd_line_opts, nullptr);
    if (opt == -1) {
      break;
    }

    switch (opt) {
    case 't':
      tmp_dir = optarg;
      break;
    case 'b':
      bitrate = optarg;
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

  if (bitrate.empty()) {
    print_usage(argv[0]);
    cerr << "Error: -b <bitrate> is required" << endl;
    return EXIT_FAILURE;
  }

  string input_filepath = argv[optind];
  string output_dir = argv[optind + 1];

  string output_filename = fs::path(input_filepath).stem().string() + ".webm";
  string tmp_filepath = fs::path(tmp_dir) / output_filename;
  string output_filepath = fs::path(output_dir) / output_filename;

  /* encode audio and output to tmp_dir first */
  vector<string> args {
    "ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "panic", "-y",
    "-i", input_filepath, "-c:a", "libopus", "-b:a", bitrate,
    "-cluster_time_limit", "5000", tmp_filepath };
  cerr << "$ " + command_str(args, {}) << endl;
  run("ffmpeg", args, {}, true, true);

  /* move the output encoded audio from tmp_dir to output_dir */
  fs::rename(tmp_filepath, output_filepath);

  return EXIT_SUCCESS;
}
