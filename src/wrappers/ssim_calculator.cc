#include <getopt.h>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include "system_runner.hh"
#include "filesystem.hh"
#include "path.hh"  /* readlink */
#include "y4m.hh"

using namespace std;

void print_usage(const string & program)
{
  cerr <<
  "Usage: " << program << " <input_path> <output_dir> [--tmp <tmp_dir>] "
  "--canonical <dir>\n"
  "Calculate SSIM between video <input_path> and canonical video <path>\n\n"
  "<input_path>    path of the input encoded video\n"
  "<output_dir>    target directory to output the SSIM\n\n"
  "Options:\n"
  "--tmp <tmp_dir>      replace default temporary directory with <tmp_dir>\n"
  "--canonical <dir>    directory of the canonical video in Y4M"
  << endl;
}

int main(int argc, char * argv[])
{
  /* parse arguments */
  if (argc < 1) {
    abort();
  }

  string tmp_dir;
  string canonical_dir;

  const option cmd_line_opts[] = {
    {"tmp",         required_argument, nullptr, 't'},
    {"canonical",   required_argument, nullptr, 'c'},
    { nullptr,      0,                 nullptr,  0 }
  };

  while (true) {
    const int opt = getopt_long(argc, argv, "t:c:", cmd_line_opts, nullptr);
    if (opt == -1) {
      break;
    }

    switch (opt) {
    case 't':
      tmp_dir = optarg;
      break;
    case 'c':
      canonical_dir = optarg;
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

  if (canonical_dir.empty()) {
    print_usage(argv[0]);
    cerr << "Error: --canonical <dir> is required" << endl;
    return EXIT_FAILURE;
  }

  string input_filepath = argv[optind];
  string output_dir = argv[optind + 1];

  string input_filestem = fs::path(input_filepath).stem().string();
  string output_filename = input_filestem + ".ssim";
  string tmp_filepath = fs::path(tmp_dir) / output_filename;
  string output_filepath = fs::path(output_dir) / output_filename;

  /* path of the ssim program */
  auto exe_dir = fs::path(roost::readlink("/proc/self/exe")).parent_path();
  string ssim = fs::canonical(exe_dir / "../ssim/ssim");

  /* get width, height and frame rate of the canonical video */
  string canonical_path = fs::path(canonical_dir) / (input_filestem + ".y4m");

  Y4MParser y4m_parser(canonical_path);
  int width = y4m_parser.get_frame_width();
  int height = y4m_parser.get_frame_height();
  float frame_rate = y4m_parser.get_frame_rate_float();

  /* scale input_filepath to a Y4M with the same resolution */
  string scaled_y4m = fs::path(tmp_dir) / (input_filestem + ".y4m");
  string scale = to_string(width) + ":" + to_string(height);
  vector<string> ffmpeg_args {
    "ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "panic", "-y",
    "-i", input_filepath, "-vf", "scale=" + scale, scaled_y4m };
  cerr << "$ " + command_str(ffmpeg_args, {}) + "\n";
  run("ffmpeg", ffmpeg_args);

  /* get the max step size that still makes sure 5 frames per second
   * are used for SSIM calculation */
  string step_size = to_string(static_cast<int>(floor(frame_rate / 5.0f)));

  /* run ssim program */
  vector<string> ssim_args {
    ssim, scaled_y4m, canonical_path, tmp_filepath, "-n", step_size };
  cerr << "$ " + command_str(ssim_args, {}) + "\n";
  run(ssim, ssim_args);

  /* remove scaled_y4m */
  fs::remove(scaled_y4m);

  /* move the output SSIM from tmp_dir to output_dir */
  fs::rename(tmp_filepath, output_filepath);

  return EXIT_SUCCESS;
}
