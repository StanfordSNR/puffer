#include "webm_info.hh"
#include <iostream>

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
  WebmInfo info(argv[1]);
  info.print_info();
  return EXIT_SUCCESS;
}
