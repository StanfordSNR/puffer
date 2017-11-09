#include <iostream>
#include <string>
#include <memory>
#include <stdexcept>
#include <getopt.h>

#include "mkvparser/mkvparser.h"
#include "mkvparser/mkvreader.h"
#include "mkvmuxer/mkvmuxer.h"
#include "mkvmuxer/mkvmuxerutil.h"
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

void create_init_segment(mkvparser::MkvReader * reader,
                         mkvmuxer::MkvWriter * writer)
{
  long long pos = 0;

  /* parse and write EBML header */
  mkvparser::EBMLHeader ebml_header;
  long long ret = ebml_header.Parse(reader, pos);
  if (ret) {
    throw runtime_error("EBMLHeader::Parse() failed");
  }
  mkvmuxer::WriteEbmlHeader(writer, ebml_header.m_docTypeVersion,
                            ebml_header.m_docType);

  /* parse Segment element but stop at the first Cluster element */
  mkvparser::Segment * parser_segment_raw;
  ret = mkvparser::Segment::CreateInstance(reader, pos, parser_segment_raw);
  if (ret) {
    throw runtime_error("Segment::CreateInstance() failed");
  }

  std::unique_ptr<mkvparser::Segment> parser_segment(parser_segment_raw);
  ret = parser_segment->ParseHeaders();
  if (ret < 0) {
    throw runtime_error("Segment::ParseHeaders() failed");
  }

  /* get Segment Info element */
  auto parser_info = parser_segment->GetInfo();
  if (not parser_info) {
    throw runtime_error("Segment::GetInfo() failed");
  }

  /* get Tracks element */
  auto parser_tracks = parser_segment->GetTracks();
  if (not parser_tracks) {
    throw runtime_error("Segment::GetTracks() failed");
  }

  /* get Tags element */
  auto parser_tags = parser_segment->GetTags();
  if (not parser_tags) {
    throw runtime_error("Segment::GetTags() failed");
  }

  /* create muxer for Segment */
  auto muxer_segment = make_unique<mkvmuxer::Segment>();
  if (not muxer_segment->Init(writer)) {
    throw runtime_error("failed to initialize muxer segment");
  }

  /* write Segment header */
  if (WriteID(writer, libwebm::kMkvSegment)) {
    throw runtime_error("WriteID");
  }

  if (SerializeInt(writer, mkvmuxer::kEbmlUnknownValue, 8)) {
    throw runtime_error("SerializeInt");
  }

  /* write Segment Info with no duration in particular */
  auto muxer_info = muxer_segment->GetSegmentInfo();
  muxer_info->set_timecode_scale(parser_info->GetTimeCodeScale());
  muxer_info->Write(writer);

  // TODO: copy Tracks element
  // TODO: remove the Tag with DURATION as TagName and copy the other tags
}

void fragment(const string & input_webm,
              const string & init_segment,
              const string & media_segment)
{
  mkvparser::MkvReader reader;
  if (reader.Open(input_webm.c_str())) {
    throw runtime_error("error while opening " + input_webm);
  }

  if (not init_segment.empty()) {
    mkvmuxer::MkvWriter writer;
    if (not writer.Open(init_segment.c_str())) {
      throw runtime_error("error while opening " + init_segment);
    }

    create_init_segment(&reader, &writer);
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
