#include <iostream>
#include <fstream>
#include <stdio.h>
#include <sys/stat.h>
#include <memory>
#include <getopt.h>
#include <set>
#include <map>
#include <cstdio>
#include <algorithm>
#include <ctime>
#include <unistd.h>

#include "system_runner.hh"
#include "tokenize.hh"

#define DUMP_FASTSSIM "dump_fastssim"
#define DUMP_SSIM "dump_ssim"
#define DAALA "daala_tools"

using namespace std;

void print_usage(const string & program_name)
{
  cerr << "Usage: " << program_name << "[options] <video1> <video2> <output>"
    << endl;
  cerr << endl;
  cerr << "\t<video1> Video segments. If it is not in Y4M, it will be converted to via ffmpeg" << endl;
  cerr << "\t<video2> Video segments. If it is not in Y4M, it will be converted to via ffmpeg" << endl;
  cerr << "\t<output> Output text file containing SSIM information" << endl;
  cerr << "Options:" << endl;
  cerr << "\t-x -fast-ssim                  Allow program to compute fast ssimi." << endl;
  cerr << "\t-c --show-chroma               Also show values for the chroma channels."  << endl;
  cerr << "\t-f --frame-type                Show frame type and QI value for each Theora frame." << endl;
  cerr << "\t-r --raw                       Show raw SSIM scores, instead of 10*log10(1/(1-ssim))." << endl;
  cerr << "\t-s --summary                   Only output the summary line." << endl;
  cerr << "\t-y --luma-only                 Only output values for the luma channel." << endl;
  cerr << "\t                                   Will be ignored when fast ssim is used (-x)" << endl;
  cerr << "\t-p <npar>, --parallel=<npar>   Run <npar> parallel workers." << endl;
  cerr << "\t                                   Will be ignored when fast ssim is used (-x)" << endl;
  cerr << "\t-l <lim>, --limit=<lim>        Stop after <lim> frames." << endl;
  cerr << "\t                                   Will be ignored when fast ssim is used (-x)" << endl;
}

bool is_y4m(const string & filename)
{
  auto str_len = filename.length();
  if(str_len <= 4)
    return false; /* no y4m extension found */
  else
    return filename.substr(str_len - 4, str_len).compare(".y4m") == 0;
}

string random_string( size_t length )
{
  auto randchar = []() -> char
  {
    const char charset[] =
      "0123456789"
      "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
      "abcdefghijklmnopqrstuvwxyz";
    const size_t max_index = (sizeof(charset) - 1);
    return charset[ rand() % max_index ];
  };
  std::string str(length,0);
  std::generate_n( str.begin(), length, randchar );
  return str;
}

string convert_y4m(const string & filename)
{
  /* can't use tempfile because it will automatically touch the file,
   * which will make ffmpeg nervous
   auto temp = make_unique<TempFile>("/tmp/y4m");
   */
  /* use time to generate unique file name */
  string temp = "/tmp/" + random_string(10) + ".y4m";
  const string command = "ffmpeg -i " + filename + " -f yuv4mpegpipe " + temp;
  auto args = split(command, " ");
  cout << "Using ffmpeg command: " << command << endl;
  run("ffmpeg", args, {}, true, true);
  return temp;
}

inline bool file_exists(const string & name)
{
  struct stat buffer;
  return (stat (name.c_str(), &buffer) == 0);
}

const char *optstring = "xcfrsyp:l:";
const struct option options[]={
  {"fast-ssim",no_argument,NULL,'x'},
  {"show-chroma",no_argument,NULL,'c'},
  {"frame-type",no_argument,NULL,'f'},
  {"raw",no_argument,NULL,'r'},
  {"summary",no_argument,NULL,'s'},
  {"luma-only",no_argument,NULL,'y'},
  {"parallel",required_argument,NULL,'p'},
  {"limit",required_argument,NULL,'l'},
  {NULL,0,NULL,0}
};


int main(int argc, char * argv[])
{
  string video_path1;
  string video_path2;
  char * output_file;
  string ssim_command = "";
  set<char> ssim_args;
  map<char, int> ssim_args_;

  bool use_temp1 = false;
  bool use_temp2 = false;

  bool fast_ssim = false;
  int long_option_index;
  int c;

  while((c=getopt_long(argc,argv,optstring,options,&long_option_index))!=EOF){
    switch(c){
      case 'x': fast_ssim = true; break;
      case 'f': ssim_args.insert('f'); break;
      case 'r': ssim_args.insert('r'); break;
      case 's': ssim_args.insert('s'); break;
      case 'c': ssim_args.insert('c'); break;
      case 'y': ssim_args.insert('y'); break;
      case 'p': ssim_args_.insert(make_pair('p', stoi(optarg))); break;
      case 'l': ssim_args_.insert(make_pair('l', stoi(optarg))); break;
      default:
                print_usage(argv[0]);
                return EXIT_FAILURE;
    }
  }

  if(argc != optind + 3) {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }
  video_path1 = string(argv[optind]);
  video_path2 = string(argv[optind + 1]);
  output_file = argv[optind + 2];

  /* set up a random seed */
  srand(std::time(nullptr));

  if(!is_y4m(video_path1)) {
    cerr << "Converting " << video_path1 << " to ";
    video_path1 = convert_y4m(video_path1);
    cerr << video_path1 << endl;
    use_temp1 = true;
  }
  if(!is_y4m(video_path2)) {
    cerr << "Converting " << video_path2 << " to ";
    video_path2 = convert_y4m(video_path1);
    cerr << video_path2 << endl;
    use_temp2 = true;
  }

  /* get current working directory to avoid pwd issue */
  char buf[FILENAME_MAX];
  size_t size = readlink("/proc/self/exe", buf, sizeof(buf));
  if(!size)
    throw runtime_error("Unable to read executable's path");

  string ssim_path = string(buf);
  size_t p_index = ssim_path.find_last_of("/");
  string cwd = ssim_path.substr(0, p_index + 1);

  const string dump = cwd + "/" + string(DAALA) + "/" + (fast_ssim ?
      string(DUMP_FASTSSIM): string(DUMP_SSIM));
  if (!file_exists(dump)) {
    cerr << "Unable to find " << dump << endl;
    cerr << "Please do $ git submodule update --init --recursive" << endl;
    cerr << "And then do make in the root folder" << endl;
    return EXIT_FAILURE;
  }
  /* filter out the args depends on which version of ssim used */
  if(fast_ssim) {
    auto a = {'y', 'p', 'l'};
    for(auto c: a)
      ssim_args.erase(c);
  } else {
    ssim_args.erase('c');
  }
  for(auto c: ssim_args) {
    ssim_command += " -";
    ssim_command += c;
  }
  for(auto entry: ssim_args_) {
    ssim_command += " -";
    ssim_command += entry.first;
    ssim_command += " ";
    ssim_command += to_string(entry.second);
  }
  ssim_command += " ";

  const string command = dump + ssim_command + video_path1 + " " +
    video_path2; // + " > " +	output_file;

  auto s_args = split(command, " ");

  ofstream of;
  of.open(output_file);
  string result = run(dump, s_args, {}, false, false, true, false);
  of << result;
  of.close();

  /* clean up */
  if(use_temp1) {
    if(!remove(video_path1.c_str())) {
      throw runtime_error("Not able to remove file: " + video_path1);
    }
  }
  if(use_temp2) {
    if(!remove(video_path2.c_str())) {
      throw runtime_error("Not able to remove file: " + video_path2);
    }
  }
  return 0;
}
