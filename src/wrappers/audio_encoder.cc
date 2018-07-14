#include <getopt.h>
#include <iostream>
#include <string>
#include <vector>

#include "filesystem.hh"
#include "child_process.hh"

using namespace std;

void print_usage(const string & program)
{
  cerr <<
  "Usage: " << program << " <input_path> <output_path> -b <bitrate>\n"
  "Encode the audio <input_path> and output to <output_path>\n\n"
  "<input_path>     path of the input raw audio\n"
  "<output_path>    path to output the encoded audio\n\n"
  "Options:\n"
  "-b <bitrate>     bitrate (e.g., 64k)"
  << endl;
}

int main(int argc, char * argv[])
{
  /* parse arguments */
  if (argc < 1) {
    abort();
  }

  string bitrate;

  const option cmd_line_opts[] = {
    {"bitrate", required_argument, nullptr, 'b'},
    { nullptr,  0,                 nullptr,  0 }
  };

  while (true) {
    const int opt = getopt_long(argc, argv, "b:", cmd_line_opts, nullptr);
    if (opt == -1) {
      break;
    }

    switch (opt) {
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

  if (bitrate.empty()) {
    print_usage(argv[0]);
    cerr << "Error: -b <bitrate> is required" << endl;
    return EXIT_FAILURE;
  }

  string input_path = argv[optind];
  string output_path = argv[optind + 1];

  string tmp_opus_name = fs::path(output_path).stem().string() + ".opus";
  string tmp_opus_path = fs::path(output_path).parent_path() / tmp_opus_name;

  ProcessManager proc_manager;

  /* workaround: encode using opusenc first.
   * Related bug of FFmpeg: https://trac.ffmpeg.org/ticket/6841. */
  vector<string> args1 { "opusenc", "--quiet", input_path, tmp_opus_path };
  int ret_code = proc_manager.run("opusenc", args1);
  if (ret_code != 0) {
    return ret_code;
  }

  /* encode audio */
  vector<string> args2 {
    "ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "panic", "-y",
    "-i", tmp_opus_path, "-c:a", "libopus", "-b:a", bitrate,
    "-avoid_negative_ts", "make_zero", output_path };

  ret_code = proc_manager.run("ffmpeg", args2);

  /* remove the temporary opus audio */
  fs::remove(tmp_opus_path);

  return ret_code;
}
