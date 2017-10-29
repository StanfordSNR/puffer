#include <iostream>
#include <string>
#include <memory>
#include <stdexcept>
#include <getopt.h>

#include "strict_conversions.hh"
#include "mp4_parser.hh"

using namespace std;

void print_usage(const string & program_name)
{
  cerr <<
  "Usage: " << program_name << " [options] <file.mp4>\n\n"
  "<file.mp4>    MP4 file to split\n\n"
  "Options:\n"
  "--init-segment <file.mp4>     initial segment\n"
  "--media-segment <template>    filename template (one and only one %u is \n"
  "                              allowed, e.g., segment-%u.m4s)\n"
  "--start-number <n>            start number in template (default 1)"
  << endl;
}

void verify(const string & media_seg)
{
  /* one and only one %u should be in the media segment template */
  size_t pos = media_seg.find('%');

  if (pos == string::npos or pos == media_seg.length() - 1) {
    throw runtime_error("invalid segment template");
  }

  if (media_seg.at(pos + 1) != 'u') {
    throw runtime_error("invalid segment template");
  }

  pos = media_seg.find('%', pos + 1);

  if (pos != string::npos) {
    throw runtime_error("invalid segment template");
  }
}

int main(int argc, char * argv[])
{
  if (argc < 1) {
    abort();
  }

  string init_seg, media_seg, input_mp4;
  unsigned int start_number = 1;

  const option cmd_line_opts[] = {
    {"init-segment",  required_argument, nullptr, 'i'},
    {"media-segment", required_argument, nullptr, 'm'},
    {"start-number",  required_argument, nullptr, 's'},
    { nullptr,        0,                 nullptr,  0 }
  };

  while (true) {
    const int opt = getopt_long(argc, argv, "i:m:s:", cmd_line_opts, nullptr);
    if (opt == -1) {
      break;
    }

    switch (opt) {
    case 'i':
      init_seg = optarg;
      break;
    case 'm':
      media_seg = optarg;
      break;
    case 's':
      start_number = narrow_cast<unsigned int>(stoi(optarg));
      break;
    default:
      print_usage(argv[0]);
      return EXIT_FAILURE;
    }
  }

  if (optind >= argc) {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  if (init_seg.empty() or media_seg.empty()) {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  verify(media_seg);

  input_mp4 = argv[optind];
  auto parser = make_unique<MP4::MP4Parser>(input_mp4);

  parser->parse();
  parser->split(init_seg, media_seg, start_number);

  return EXIT_SUCCESS;
}
