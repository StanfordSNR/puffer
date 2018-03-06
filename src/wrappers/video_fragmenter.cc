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
  "Usage: " << program << " <input_path> <output_path>\n"
  "Fragment the video <input_path> and output to <output_path>\n\n"
  "<input_path>     path of the input encoded video\n"
  "<output_path>    path to output the fragmented video"
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
  string output_path = argv[2];
  string init_path = fs::path(output_path).parent_path() / "init.mp4";

  /* path of the mp4_fragment program */
  auto exe_dir = fs::path(roost::readlink("/proc/self/exe")).parent_path();
  string mp4_fragment = fs::canonical(exe_dir / "../mp4/mp4_fragment");

  /* fragment video */
  vector<string> args = { mp4_fragment, input_path, "-m", output_path };

  /* generate initialization segment only if it does not already exist */
  if (not fs::exists(init_path)) {
    args.emplace_back("-i");
    args.emplace_back(init_path);
  }

  ProcessManager proc_manager;
  return proc_manager.run(mp4_fragment, args);
}
