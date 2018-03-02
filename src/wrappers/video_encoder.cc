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
  "-s <resolution> --crf <CRF>\n"
  "Encode the video <input_path> and output to <output_dir>\n\n"
  "<input_path>    path of the input canonical video\n"
  "<output_dir>    target directory to output the encoded video\n\n"
  "Options:\n"
  "--tmp <tmp_dir>    replace the default temporary directory with <tmp_dir>\n"
  "-s <resolution>    resolution (e.g., 1280x720)\n"
  "--crf <CRF>        constant rate factor"
  << endl;
}

int main(int argc, char * argv[])
{
  /* parse arguments */
  if (argc < 1) {
    abort();
  }

  string tmp_dir;
  string resolution;
  string crf;

  const option cmd_line_opts[] = {
    {"tmp",    required_argument, nullptr, 't'},
    {"res",    required_argument, nullptr, 's'},
    {"crf",    required_argument, nullptr, 'c'},
    { nullptr, 0,                 nullptr,  0 }
  };

  while (true) {
    const int opt = getopt_long(argc, argv, "t:s:c:", cmd_line_opts, nullptr);
    if (opt == -1) {
      break;
    }

    switch (opt) {
    case 't':
      tmp_dir = optarg;
      break;
    case 's':
      resolution = optarg;
      break;
    case 'c':
      crf = optarg;
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

  if (resolution.empty() or crf.empty()) {
    print_usage(argv[0]);
    cerr << "Error: -s <resolution> and --crf <CRF> are both required" << endl;
    return EXIT_FAILURE;
  }

  string input_filepath = argv[optind];
  string output_dir = argv[optind + 1];

  string output_filename = fs::path(input_filepath).stem().string() + ".mp4";
  string tmp_filepath = fs::path(tmp_dir) / output_filename;
  string output_filepath = fs::path(output_dir) / output_filename;

  /* encode video and output to tmp_dir first */
  vector<string> args {
    "ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "panic", "-y",
    "-i", input_filepath, "-c:v", "libx264", "-s", resolution, "-crf", crf,
    "-preset", "veryfast", tmp_filepath };
  cerr << "$ " + command_str(args, {}) + "\n";
  run("ffmpeg", args);

  /* move the output encoded video from tmp_dir to output_dir */
  fs::rename(tmp_filepath, output_filepath);

  return EXIT_SUCCESS;
}
