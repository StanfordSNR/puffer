#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <getopt.h>
#include <fcntl.h>

#include "path.hh"
#include "system_runner.hh"
#include "tokenize.hh"
#include "file_descriptor.hh"
#include "temp_file.hh"
#include "exception.hh"

using namespace std;

void print_usage(const string & program_name)
{
  cerr <<
  "Usage: " << program_name << " [options] <video1> <video2> <output>\n\n"
  "<video1>, <video2>    Y4M files; will be converted to Y4M files if not\n"
  "<output>              output text file containing SSIM index\n\n"
  "Options:\n"
  "--fast-ssim              compute fast SSIM instead\n"
  "-p, --parallel <npar>    run <npar> parallel workers\n"
  "                         (ignored when --fast-ssim is used)\n"
  "-l, --limit <lim>        stop after <lim> frames\n"
  "                         (ignored when --fast-ssim is used)"
  << endl;
}

string get_ssim_path(bool fast_ssim)
{
  /* get path to daala_tools based on the path of executable */
  roost::path exe_path = roost::dirname(roost::readlink("/proc/self/exe"));
  roost::path daala_tools_path = exe_path / "../../third_party/daala_tools";

  string ssim_filename = fast_ssim ? "dump_fastssim" : "dump_ssim";
  string ssim_path = (daala_tools_path / ssim_filename).string();

  if (not roost::exists(ssim_path)) {
    throw runtime_error("unable to find " + ssim_path);
  }

  return ssim_path;
}

unique_ptr<TempFile> convert_to_y4m(const string & video_path)
{
  if (split_filename(video_path).second == "y4m") {
    return nullptr;
  }

  auto tmp_y4m = make_unique<TempFile>("/tmp/y4m");

  string cmd = "ffmpeg -y -i " + video_path + " -f yuv4mpegpipe " +
               tmp_y4m->name();
  cerr << "$ " << cmd << endl;

  auto args = split(cmd, " ");
  run("ffmpeg", args, {}, true, true);

  return move(tmp_y4m);
}

void write_to_file(const string & output_path, const string & result)
{
  FileDescriptor output_fd(
    CheckSystemCall("open (" + output_path + ")",
                    open(output_path.c_str(), O_WRONLY | O_CREAT, 0644)));
  output_fd.write(result, true);
  output_fd.close();
}

int main(int argc, char * argv[])
{
  if (argc < 1) {
    abort();
  }

  bool fast_ssim = false;
  string parallel_cnt, frame_limit;

  const option cmd_line_opts[] = {
    {"fast-ssim", no_argument,       nullptr, 'x'},
    {"parallel",  required_argument, nullptr, 'p'},
    {"limit",     required_argument, nullptr, 'l'},
    { nullptr,    0,                 nullptr,  0 }
  };

  while (true) {
    const int opt = getopt_long(argc, argv, "xp:l:", cmd_line_opts, nullptr);
    if (opt == -1) {
      break;
    }

    switch (opt) {
    case 'x':
      fast_ssim = true;
      break;
    case 'p':
      parallel_cnt = optarg;
      break;
    case 'l':
      frame_limit = optarg;
      break;
    default:
      print_usage(argv[0]);
      return EXIT_FAILURE;
    }
  }

  if (optind != argc - 3) {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  string video1_path = argv[optind];
  string video2_path = argv[optind + 1];
  string output_path = argv[optind + 2];

  /* convert input videos to Y4M if necessary */
  unique_ptr<TempFile> video1_tmp = convert_to_y4m(video1_path);
  if (video1_tmp != nullptr) {
    video1_path = video1_tmp->name();
  }

  unique_ptr<TempFile> video2_tmp = convert_to_y4m(video2_path);
  if (video2_tmp != nullptr) {
    video2_path = video2_tmp->name();
  }

  /* construct command to run dump_ssim or dump_fastssim */
  string ssim_path = get_ssim_path(fast_ssim);

  string cmd = ssim_path + " -s";
  if (not fast_ssim) {
    if (not parallel_cnt.empty()) {
      cmd += " -p " + parallel_cnt;
    }

    if (not frame_limit.empty()) {
      cmd += " -l " + frame_limit;
    }
  }
  cmd += " " + video1_path + " " + video2_path;
  cerr << "$ " << cmd << endl;

  /* dump SSIM index to the output file */
  auto args = split(cmd, " ");
  string ssim_result = run(ssim_path, args, {}, true, true, true);
  write_to_file(output_path, ssim_result);

  return EXIT_SUCCESS;
}
