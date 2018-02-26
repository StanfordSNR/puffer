#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include <tuple>
#include <regex>

#include "system_runner.hh"
#include "filesystem.hh"

using namespace std;

void print_usage(const string & program)
{
  cerr <<
  "Usage: " << program << " <input_path> <output_dir> [--tmp <tmp_dir>] "
  "--canonical <path>\n"
  "Calculate SSIM between video <input_path> and canonical video <path>\n\n"
  "<input_path>    path of the input encoded video\n"
  "<output_dir>    target directory to output the SSIM\n\n"
  "Options:\n"
  "--tmp <tmp_dir>       replace default temporary directory with <tmp_dir>\n"
  "--canonical <path>    path of the canonical video in Y4M"
  << endl;
}

/* parse the first line of y4m_path and get frame width and height */
tuple<string, string> get_width_height(const string & y4m_path)
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

  /* use regex to find " W<integer>" and " H<integer>" */
  smatch matches;
  string width, height;

  if (regex_search(line, matches, regex(" W(\\d+)\\s"))) {
    assert(matches.size() == 2);
    width = matches[1].str();
  } else {
    throw runtime_error(y4m_path + " : no frame width found");
  }

  if (regex_search(line, matches, regex(" H(\\d+)\\s"))) {
    assert(matches.size() == 2);
    height = matches[1].str();
  } else {
    throw runtime_error(y4m_path + " : no frame height found");
  }

  return {width, height};
}

int main(int argc, char * argv[])
{
  /* parse arguments */
  if (argc < 1) {
    abort();
  }

  string tmp_dir;
  string canonical_path;

  const option cmd_line_opts[] = {
    {"tmp",            required_argument, nullptr, 't'},
    {"canonical_path", required_argument, nullptr, 'c'},
    { nullptr,         0,                 nullptr,  0 }
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
      canonical_path = optarg;
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

  if (canonical_path.empty()) {
    print_usage(argv[0]);
    cerr << "Error: --canonical <path> is required" << endl;
    return EXIT_FAILURE;
  }

  /* get width and height of the canonical video */
  auto [width, height] = get_width_height(canonical_path);
  cerr << width << " " << height << endl;

  string input_filepath = argv[optind];
  string output_dir = argv[optind + 1];

  string output_filename = fs::path(input_filepath).stem().string() + ".ssim";
  string tmp_filepath = fs::path(tmp_dir) / output_filename;
  string output_filepath = fs::path(output_dir) / output_filename;

  return EXIT_SUCCESS;
}
