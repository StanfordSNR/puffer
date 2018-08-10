#include <getopt.h>
#include <iostream>
#include <string>
#include <vector>

#include "child_process.hh"
#include "filesystem.hh"

using namespace std;

void print_usage(const string & program)
{
  cerr <<
  "Usage: " << program << " <input_path> <output_path> "
  "-s <resolution> --crf <CRF>\n"
  "Encode the video <input_path> and output to <output_path>\n\n"
  "<input_path>     path of the input canonical video\n"
  "<output_path>    path to output the encoded video\n\n"
  "Options:\n"
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

  string resolution;
  string crf;

  const option cmd_line_opts[] = {
    {"res",    required_argument, nullptr, 's'},
    {"crf",    required_argument, nullptr, 'c'},
    { nullptr, 0,                 nullptr,  0 }
  };

  while (true) {
    const int opt = getopt_long(argc, argv, "s:c:", cmd_line_opts, nullptr);
    if (opt == -1) {
      break;
    }

    switch (opt) {
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

  if (resolution.empty() or crf.empty()) {
    print_usage(argv[0]);
    cerr << "Error: -s <resolution> and --crf <CRF> are both required" << endl;
    return EXIT_FAILURE;
  }

  string input_path = argv[optind];
  string output_path = argv[optind + 1];

  /* encode video */
  vector<string> args {
    "ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "warning", "-y",
    "-i", input_path, "-c:v", "libx264", "-s", resolution, "-crf", crf,
    "-preset", "veryfast", "-threads", "1", output_path };

  ProcessManager proc_manager;
  return proc_manager.run("ffmpeg", args);
}
