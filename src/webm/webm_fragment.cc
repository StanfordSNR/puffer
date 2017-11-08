#include <iostream>
#include <string>

#include "webm/callback.h"
#include "webm/file_reader.h"
#include "webm/status.h"
#include "webm/webm_parser.h"

using namespace std;
using namespace webm;

void print_usage(const string & program_name)
{
  cerr <<
  "Usage: " << program_name << " <file.webm>\n\n"
  "<file.webm>    WebM file to parse"
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

  return EXIT_SUCCESS;
}
