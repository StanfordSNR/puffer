#include <iostream>
#include <string>
#include <vector>

#include "child_process.hh"
#include "filesystem.hh"
#include "y4m.hh"

using namespace std;

void print_usage(const string & program)
{
  cerr <<
  "Usage: " << program << " <input_path> <output_path>\n"
  "Canonicalize the video <input_path> and output to <output_path>\n\n"
  "<input_path>     path of the input raw video\n"
  "<output_path>    path to output the canonical video"
  << endl;
}

int main(int argc, char * argv[])
{
  /* parse arguments */
  if (argc < 1) {
    abort();
  }

  if (argc != 3) {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  string input_path = argv[1];
  string output_path= argv[2];

  /* parse header of the input Y4M */
  Y4MParser y4m_parser(input_path);

  /* canonicalize video */
  vector<string> args {
    "ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "panic", "-y",
    "-i", input_path };

  /* deinterlace only if Y4M is interlaced */
  if (y4m_parser.is_interlaced()) {
    args.emplace_back("-vf");
    args.emplace_back("bwdif");
  }

  args.emplace_back(output_path);

  ProcessManager proc_manager;
  int ret_code = proc_manager.run("ffmpeg", args);

  /* remove the input raw video */
  fs::remove(input_path);

  return ret_code;
}
