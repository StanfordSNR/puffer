#include <iostream>
#include <string>
#include <memory>

#include "mp4_parser.hh"

using namespace std;

void print_usage(const string & program_name)
{
  cerr <<
  "Usage: " << program_name << " <file.mp4>\n\n"
  "<file.mp4>    MP4 file to parse"
  << endl;
}

int main(int argc, char * argv[])
{
  if (argc < 1) {
    abort();
  }

  if (argc != 2) {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  auto parser = make_unique<MP4::MP4Parser>(argv[1]);
  parser->parse();
  parser->print_structure();

  return EXIT_SUCCESS;
}
