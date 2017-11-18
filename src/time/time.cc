#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <iostream>
#include <vector>
#include <getopt.h>
#include <chrono>

#include "file_descriptor.hh"
#include "tokenize.hh"
#include "exception.hh"

using namespace std;

const uint32_t default_timescale = 90000;
const uint32_t buffer_size = 64;

void print_usage(const string & program_name)
{
  cerr
    << "Usage: " << program_name << " [options]\n"
    "-i --input <input>      input file in <time>.m4s format.\n"
    "-o --output <output>    program will write the timefile to <output>.\n"
    "-s --scale <timescale>  presentation time scale.\n";
}

string format_time(const time_t time)
{
  char buf[buffer_size];
  tm now_tm;
  gmtime_r(&time, &now_tm);
  strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &now_tm);
  return buf;
}

int main(int argc, char * argv[])
{
  string timescale = "";
  string input = "";
  string output = "";
  uint32_t i_timescale, i_time;

  int opt, long_option_index;

  const char *optstring = "o:i:s:";
  const struct option options[] = {
    {"output",            required_argument, nullptr, 'o'},
    {"input",             required_argument, nullptr, 'i'},
    {"scale",             required_argument, nullptr, 's'},
    { nullptr,            0,                 nullptr,  0 },
  };

  while (true) {
    opt = getopt_long(argc, argv, optstring, options, &long_option_index);
    if (opt == EOF) {
      break;
    }
    switch (opt) {
      case 'o':
        output = optarg;
        break;
      case 'i':
        input = optarg;
        break;
      case 's':
        timescale = optarg;
        break;
      default:
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
  }

  if (output == "") {
    cout << "Missing -o <output>" << endl;
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }
  if (input == "") {
    cout << "Missing -i <input>" << endl;
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  string time_string = split_filename(input).first;
  i_time = stoi(time_string);

  if (timescale != "") {
    i_timescale = stoi(timescale);
  } else {
    i_timescale = default_timescale;
  }

  /* 1. obtain the lock for the output
   * 2. compare the time file time and input time
   * 3. if input time is >= the time file, flush the time file
   * 4. release the lock
   */
  FileDescriptor fd(CheckSystemCall("open (" + output + ")",
         open(output.c_str(), O_RDWR | O_CREAT, 0644)));
  fd.block_for_exclusive_lock();

  uint64_t filesize = fd.filesize();

  /* actual duration in seconds */
  time_t duration = static_cast<time_t>(i_time / i_timescale);

  if (filesize > 0) {
    string file_time = fd.read(buffer_size);
    /* convert to time_t */
    struct tm tm;
    strptime(file_time.c_str(), "%FT%TZ", &tm);
    time_t t = timegm(&tm);
    if (duration <= t) {
      /* no need to update */
      fd.release_flock();
      fd.close();
      return EXIT_SUCCESS;
    }
  }

  string format_string = format_time(duration);
  /* reset the file content */
  fd.reset();
  fd.write(format_string);
  fd.release_flock();
  fd.close();
  return EXIT_SUCCESS;
}
