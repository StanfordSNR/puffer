#include "webm_parser.hh"
#include <iostream>
#include <memory>

using namespace std;
using namespace webm;

void print_usage(const string & name)
{
  cerr << "Usage: " + name + " <filename>" << endl
       << "<filename>       webm file that contains an audio track" << endl;
}

int main(int argc, char * argv[])
{
  if (argc != 2) {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }
  auto p = make_unique<WebmParser>(string(argv[1]));
  return EXIT_SUCCESS;
}
