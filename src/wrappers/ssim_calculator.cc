#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <cassert>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>
#include <tuple>
#include <regex>

#include "system_runner.hh"
#include "filesystem.hh"
#include "path.hh"  /* readlink */

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

/* parse the first line of y4m_path and get width, height and frame rate */
tuple<int, int, float> parse_y4m_header(const string & y4m_path)
{
  FILE *fp = fopen(y4m_path.c_str(), "r");
  if (fp == NULL) {
    throw runtime_error("fopen failed to open " + y4m_path);
  }

  /* only the first line is needed so getline() is more efficient */
  char * line_ptr = NULL;
  size_t len = 0;
  if (getline(&line_ptr, &len, fp) == -1) {
    free(line_ptr);
    throw runtime_error("getline failed to read a line from " + y4m_path);
  }

  string line(line_ptr);
  free(line_ptr);

  /* find " W<integer>", " H<integer>", " F<numerator:denominator>" */
  smatch matches;
  int width, height;
  float frame_rate;

  if (regex_search(line, matches, regex(" W(\\d+)\\s"))) {
    assert(matches.size() == 2);
    width = stol(matches[1].str());
  } else {
    throw runtime_error(y4m_path + " : no frame width found");
  }

  if (regex_search(line, matches, regex(" H(\\d+)\\s"))) {
    assert(matches.size() == 2);
    height = stol(matches[1].str());
  } else {
    throw runtime_error(y4m_path + " : no frame height found");
  }

  if (regex_search(line, matches, regex(" F(\\d+):(\\d+)\\s"))) {
    assert(matches.size() == 3);
    frame_rate = 1.0f * stol(matches[1].str()) / stol(matches[2].str());
  } else {
    throw runtime_error(y4m_path + " : no frame rate found");
  }

  return {width, height, frame_rate};
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
  auto [width, height, frame_rate] = parse_y4m_header(canonical_path);

  /* scale input_filepath to a Y4M with the same resolution */
  string scaled_y4m = fs::path(tmp_dir) / (input_filestem + ".y4m");
  string scale = to_string(width) + ":" + to_string(height);
  vector<string> ffmpeg_args {
    "ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "panic", "-y",
    "-i", input_filepath, "-vf", "scale=" + scale, scaled_y4m };
  cerr << "$ " + command_str(ffmpeg_args, {}) + "\n";
  run("ffmpeg", ffmpeg_args, {}, true, true);

  /* get the max step size that still makes sure 5 frames per second
   * are used for SSIM calculation */
  string step_size = to_string(static_cast<int>(floor(frame_rate / 5.0f)));

  /* run ssim program */
  vector<string> ssim_args {
    ssim, scaled_y4m, canonical_path, tmp_filepath, "-n", step_size };
  cerr << "$ " + command_str(ssim_args, {}) + "\n";
  run(ssim, ssim_args, {}, true, true);

  /* move the output SSIM from tmp_dir to output_dir */
  fs::rename(tmp_filepath, output_filepath);

  /* remove scaled_y4m */
  fs::remove(scaled_y4m);

  return EXIT_SUCCESS;
}
