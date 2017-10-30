#include <iostream>
#include <string>

#include "mp4_parser.hh"

using namespace std;

void print_usage(const string & program_name)
{
  cerr <<
  "Usage: " << program_name << " <input.mp4> <output.m4s>\n\n"
  "<input.mp4>     input MP4 file to fragment\n"
  "<output.m4s>    output M4S file"
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

  auto parser = make_unique<MP4::MP4Parser>(argv[1]);
  parser->parse();
  parser->print_structure();

  return EXIT_SUCCESS;
}
