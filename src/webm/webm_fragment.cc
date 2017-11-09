#include <iostream>
#include <string>
#include <memory>
#include <stdexcept>
#include <getopt.h>

#include "mkvparser/mkvparser.h"
#include "mkvparser/mkvreader.h"

#include "mkvmuxer/mkvmuxer.h"
#include "mkvmuxer/mkvwriter.h"

using namespace std;

void print_usage(const string & program_name)
{
  cerr <<
  "Usage: " << program_name << " [options] <input_segment>\n\n"
  "<input_segment>    input WebM segment to fragment\n\n"
  "Options:\n"
  "--init-segment, -i     output initial segment\n"
  "--media-segment, -m    output media segment in the format of <num>.chk,\n"
  "                       where <num> denotes the segment number"
  << endl;
}

void create_init_segment(mkvparser::MkvReader & reader,
                         long long & pos,
                         mkvmuxer::MkvWriter & writer)
{
  /* parse and write EBML header */
  mkvparser::EBMLHeader ebml_header;
  long long ret = ebml_header.Parse(&reader, pos);
  if (ret) {
    throw runtime_error("EBMLHeader::Parse() failed");
  }
  mkvmuxer::WriteEbmlHeader(&writer, ebml_header.m_docTypeVersion,
                            ebml_header.m_docType);
}

void fragment(const string & input_webm,
              const string & init_segment,
              const string & media_segment)
{
  mkvparser::MkvReader reader;
  if (reader.Open(input_webm.c_str())) {
    throw runtime_error("error while opening " + input_webm);
  }

  long long pos = 0;

  if (not init_segment.empty()) {
    mkvmuxer::MkvWriter writer;
    if (not writer.Open(init_segment.c_str())) {
      throw runtime_error("error while opening " + init_segment);
    }

    create_init_segment(reader, pos, writer);
  }
}

int main(int argc, char * argv[])
{
  if (argc < 1) {
    abort();
  }

  string init_segment, media_segment;

  const option cmd_line_opts[] = {
    {"init-segment",  required_argument, nullptr, 'i'},
    {"media-segment", required_argument, nullptr, 'm'},
    { nullptr,        0,                 nullptr,  0 }
  };

  while (true) {
    const int opt = getopt_long(argc, argv, "i:m:", cmd_line_opts, nullptr);
    if (opt == -1) {
      break;
    }

    switch (opt) {
    case 'i':
      init_segment = optarg;
      break;
    case 'm':
      media_segment = optarg;
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

  if (init_segment.empty() and media_segment.empty()) {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  fragment(input_segment, init_segment, media_segment);

  return EXIT_SUCCESS;
}
