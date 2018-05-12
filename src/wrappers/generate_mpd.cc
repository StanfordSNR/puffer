#include <getopt.h>
#include <iostream>
#include <string>

#include "child_process.hh"
#include "filesystem.hh"
#include "path.hh"  /* readlink */

using namespace std;

void print_usage(const string & program)
{
  cerr <<
  "Usage: " << program << " [options] <input_dir>\n"
  "This program must be run from the base URL directory ('/')\n\n"
  "<input_dir>    output dir of run_pipeline\n\n"
  "Options:\n"
  "-o    output MPD filename (in the <input_dir/ready>)\n"
  "-t    filename of the time file (in the <input_dir/ready>)"
  << endl;
}

void check_input_dir(string & input_dir)
{
  /* input_dir should not start with "/" or "." */
  if (input_dir.front() == '/' or input_dir.front() == '.') {
    throw runtime_error("input_dir should not start with / or .");
  }

  /* append "/" if input_dir is not ended with a "/" */
  if (input_dir.back() != '/') {
    input_dir += '/';
  }
}

vector<string> find_media_dir_list(const string & input_dir)
{
  vector<string> ret;

  string ready_dir = fs::path(input_dir);
  for (const auto & dir : fs::directory_iterator(ready_dir)) {
    /* ignore non-directories */
    if (not fs::is_directory(dir.path())) {
      continue;
    }

    string dir_name = fs::canonical(dir.path());

    /* ignore SSIM directories */
    if (dir_name.find("ssim") == string::npos) {
      ret.emplace_back(dir_name);
    }
  }

  return ret;
}

int main(int argc, char * argv[])
{
  if (argc < 1) {
    abort();
  }

  string output_mpd = "live.mpd", timefile = "time";

  const option cmd_line_opts[] = {
    {"output", required_argument, nullptr, 'o'},
    {"time",   required_argument, nullptr, 't'},
    { nullptr, 0,                 nullptr,  0 }
  };

  while (true) {
    const int opt = getopt_long(argc, argv, "o:t:", cmd_line_opts, nullptr);
    if (opt == -1) {
      break;
    }

    switch (opt) {
    case 'o':
      output_mpd = optarg;
      break;
    case 't':
      timefile = optarg;
      break;
    default:
      print_usage(argv[0]);
      return EXIT_FAILURE;
    }
  }

  if (optind != argc - 1) {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  string input_dir = argv[optind];
  check_input_dir(input_dir);
  input_dir = input_dir + "ready/";

  /* path of the mpd_writer program */
  auto exe_dir = fs::path(roost::readlink("/proc/self/exe")).parent_path();
  string mpd_writer = fs::canonical(exe_dir / "../mpd/mpd_writer");

  /* add "/" to indicate absolute URL */
  string time_url = "/" + input_dir + timefile;

  output_mpd = input_dir + output_mpd;

  /* find directories that contain media files (not SSIM) */
  vector<string> media_dir_list = find_media_dir_list(input_dir);

  vector<string> args = { mpd_writer, "-t", time_url, "-o", output_mpd };
  args.insert(args.end(), media_dir_list.begin(), media_dir_list.end());

  ProcessManager proc_manager;
  return proc_manager.run(mpd_writer, args);
}
