#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>
#include <iostream>
#include <vector>
#include <chrono>

#include "path.hh"
#include "strict_conversions.hh"
#include "file_descriptor.hh"
#include "tokenize.hh"
#include "exception.hh"

using namespace std;

const int default_timescale = 90000;
const size_t buffer_size = 64;

void print_usage(const string & program_name)
{
  cerr <<
  "Usage: " << program_name << " [options]\n"
  "-i --input <input>      input file in <time>.m4s format.\n"
  "-o --output <output>    program will write the timefile to <output>.\n"
  "-s --scale <timescale>  presentation time scale."
  << endl;
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
  if (argc < 1) {
    abort();
  }

  string timescale, input, output;

  const option cmd_line_opts[] = {
    {"output", required_argument, nullptr, 'o'},
    {"input",  required_argument, nullptr, 'i'},
    {"scale",  required_argument, nullptr, 's'},
    { nullptr, 0,                 nullptr,  0 },
  };

  while (true) {
    const int opt = getopt_long(argc, argv, "o:i:s:", cmd_line_opts, nullptr);
    if (opt == -1) {
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

  if (optind != argc) {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  if (output.empty()) {
    cout << "Missing -o <output>" << endl;
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  if (input.empty()) {
    cout << "Missing -i <input>" << endl;
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  string time_string = roost::rbasename(input).string();
  time_string = split_filename(time_string).first;
  long long i_time = stoll(time_string);

  long long i_timescale;
  if (timescale.size()) {
    i_timescale = stoll(timescale);
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
  time_t duration = narrow_cast<time_t>(i_time / i_timescale);

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

  string updated_time = format_time(duration);
  /* reset the file content */
  fd.reset();
  fd.write(updated_time);
  fd.release_flock();
  fd.close();
  return EXIT_SUCCESS;
}
