#include <getopt.h>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include "child_process.hh"
#include "filesystem.hh"
#include "path.hh"  /* readlink */
#include "y4m.hh"

using namespace std;

void print_usage(const string & program)
{
  cerr <<
  "Usage: " << program << " <input_path> <output_path> --canonical <dir>\n"
  "Calculate SSIM between video <input_path> and canonical video <path>\n\n"
  "<input_path>     path of the input encoded video\n"
  "<output_path>    path to output the SSIM\n\n"
  "Options:\n"
  "--canonical <dir>    directory of the canonical video in Y4M"
  << endl;
}

int main(int argc, char * argv[])
{
  /* parse arguments */
  if (argc < 1) {
    abort();
  }

  string canonical_dir;

  const option cmd_line_opts[] = {
    {"canonical",   required_argument, nullptr, 'c'},
    { nullptr,      0,                 nullptr,  0 }
  };

  while (true) {
    const int opt = getopt_long(argc, argv, "c:", cmd_line_opts, nullptr);
    if (opt == -1) {
      break;
    }

    switch (opt) {
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

  if (canonical_dir.empty()) {
    print_usage(argv[0]);
    cerr << "Error: --canonical <dir> is required" << endl;
    return EXIT_FAILURE;
  }

  string input_path = argv[optind];
  string output_path = argv[optind + 1];

  string y4m_filename = fs::path(input_path).stem().string() + ".y4m";
  string canonical_path = fs::path(canonical_dir) / y4m_filename;

  /* path of the ssim program */
  auto exe_dir = fs::path(roost::readlink("/proc/self/exe")).parent_path();
  string ssim = fs::canonical(exe_dir / "../ssim/ssim");

  /* get width, height and frame rate of the canonical video */
  Y4MParser y4m_parser(canonical_path);
  int width = y4m_parser.get_frame_width();
  int height = y4m_parser.get_frame_height();

  /* scale the input video to a Y4M with the same resolution */
  string scaled_y4m = fs::path(output_path).parent_path() / y4m_filename;
  string scale = to_string(width) + ":" + to_string(height);
  vector<string> ffmpeg_args {
    "ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "warning", "-y",
    "-i", input_path, "-vf", "scale=" + scale, "-threads", "1", scaled_y4m };

  ProcessManager proc_manager;
  int ret_code = proc_manager.run("ffmpeg", ffmpeg_args);
  if (ret_code < 0) {
    return ret_code;
  }

  /* run ssim program */
  vector<string> ssim_args { ssim, scaled_y4m, canonical_path, output_path };
  ret_code = proc_manager.run(ssim, ssim_args);

  /* remove scaled_y4m */
  fs::remove(scaled_y4m);

  return ret_code;
}
