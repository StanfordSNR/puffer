#include <getopt.h>
#include <iostream>
#include <string>
#include <vector>

#include "child_process.hh"
#include "filesystem.hh"
#include "path.hh"  /* readlink */

using namespace std;

void print_usage(const string & program)
{
  cerr <<
  "Usage: " << program << " <input_path> <output_path> -i <init_path>\n"
  "Fragment the video <input_path> and output to <output_path>\n\n"
  "<input_path>     path of the input encoded video\n"
  "<output_path>    path to output the fragmented video\n\n"
  "Options:\n"
  "-i <init_path>    output an init segment to <init_path> if not exists"
  << endl;
}

int main(int argc, char * argv[])
{
  /* parse arguments */
  if (argc < 1) {
    abort();
  }

  string init_path;

  const option cmd_line_opts[] = {
    {"init",   required_argument, nullptr, 'i'},
    { nullptr, 0,                 nullptr,  0 }
  };

  while (true) {
    const int opt = getopt_long(argc, argv, "i:", cmd_line_opts, nullptr);
    if (opt == -1) {
      break;
    }

    switch (opt) {
    case 'i':
      init_path = optarg;
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

  string input_path = argv[optind];
  string output_path = argv[optind + 1];

  string tmp_init_name = fs::path(output_path).stem().string() + "-init.mp4";
  string tmp_init_path = fs::path(output_path).parent_path() / tmp_init_name;

  /* path of the mp4_fragment program */
  auto exe_dir = fs::path(roost::readlink("/proc/self/exe")).parent_path();
  string mp4_fragment = fs::canonical(exe_dir / "../mp4/mp4_fragment");

  /* fragment video */
  vector<string> args = { mp4_fragment, input_path, "-m", output_path };

  /* output a temp init segment if the dest init segment does not exist */
  bool output_tmp_init = false;
  if (not fs::exists(init_path)) {
    args.emplace_back("-i");
    args.emplace_back(tmp_init_path);
    output_tmp_init = true;
  }

  ProcessManager proc_manager;
  int ret_code = proc_manager.run(mp4_fragment, args);

  /* move the init segment from temporary path to target path */
  if (output_tmp_init) {
    fs::rename(tmp_init_path, init_path);
  }

  return ret_code;
}
