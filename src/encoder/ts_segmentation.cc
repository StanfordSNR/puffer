#include <iostream>
#include <string>
#include <vector>

#include "tokenize.hh"
#include "system_runner.hh"
#include "tokenize.hh"

using namespace std;

void print_usage(const string & program_name)
{
  cerr << "Usage: " << program_name << " <tcp_port> <output_segment.ts>"
       << endl << endl
       << "<tcp_port>             TCP port on localhost to listen on" << endl
       << "<output_segment.ts>    segment filename template, e.g., seg-%03d.ts"
       << endl;
}

int main(int argc, char * argv[])
{
  if (argc < 1) {
    abort();
  }

  if (argc != 3) {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  string tcp_port{argv[1]}, output{argv[2]};

  string args_str = "ffmpeg -y -i tcp://localhost:" + tcp_port + "?listen " +
                    "-map 0 -c copy -f segment -segment_format mpegts " +
                    "-segment_time 2 " + output;
  cerr << "$ " << args_str << endl;

  vector<string> args = split(args_str, " ");
  run("ffmpeg", args, {}, true, true);

  return EXIT_SUCCESS;
}
