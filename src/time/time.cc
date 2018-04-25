#include <getopt.h>
#include <time.h>
#include <fcntl.h>

#include <iostream>
#include <string>

#include "file_descriptor.hh"
#include "filesystem.hh"
#include "exception.hh"

using namespace std;

static const uint32_t global_timescale = 90000;
static const char time_format[] = "%Y-%m-%dT%H:%M:%SZ";

void print_usage(const string & program_name)
{
  cerr <<
  "Usage: " << program_name << " [options] <input_segment>\n\n"
  "<input_segment>    input media segment named timestamp.xxx\n\n"
  "Options:\n"
  "-o --output    file to output the clock time\n"
  "-d --delay     delay presentation time by <time> seconds"
  << endl;
}

/* convert string to time_t */
time_t parse_time(const string & time_str)
{
  struct tm tm;
  strptime(time_str.c_str(), time_format, &tm);
  return timegm(&tm);
}

/* convert time_t to string */
string format_time(const time_t t)
{
  struct tm tm;
  gmtime_r(&t, &tm);

  char buf[64];
  strftime(buf, sizeof(buf), time_format, &tm);
  return buf;
}

int main(int argc, char * argv[])
{
  if (argc < 1) {
    abort();
  }

  string output_timefile, presentation_delay;

  const option cmd_line_opts[] = {
    {"output", required_argument, nullptr, 'o'},
    {"delay",  required_argument, nullptr, 'd'},
    { nullptr, 0,                 nullptr,  0 },
  };

  while (true) {
    const int opt = getopt_long(argc, argv, "o:d:", cmd_line_opts, nullptr);
    if (opt == -1) {
      break;
    }

    switch (opt) {
    case 'o':
      output_timefile = optarg;
      break;
    case 'd':
      presentation_delay = optarg;
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

  string input_segment = argv[optind];

  if (output_timefile.empty()) {
    cerr << "Error: -o is required" << endl;
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  string timestamp_str = fs::path(input_segment).stem();
  int64_t timestamp = stoll(timestamp_str);

  /* duration in seconds */
  time_t duration = timestamp / global_timescale;
  if (not presentation_delay.empty()) {
    duration -= stoi(presentation_delay);
  }

  /* write to timefile if input timestamp is greater than the existing one */
  FileDescriptor fd(CheckSystemCall("open (" + output_timefile + ")",
      open(output_timefile.c_str(), O_RDWR | O_CREAT, 0644)));
  fd.acquire_exclusive_flock();

  time_t curr_duration = -1;
  string time_str = fd.read();
  if (not time_str.empty()) {
    curr_duration = parse_time(time_str);
  }

  if (curr_duration == -1 or duration > curr_duration) {
    fd.reset_offset();
    fd.write(format_time(duration));
  }

  fd.release_flock();
  fd.close();

  return EXIT_SUCCESS;
}
