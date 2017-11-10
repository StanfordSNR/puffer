#include <iostream>
#include <string>
#include <cstring>
#include <vector>
#include <utility>
#include <memory>
#include <stdexcept>
#include <getopt.h>

#include "strict_conversions.hh"

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

void create_init_segment(
    mkvmuxer::MkvWriter * writer, mkvparser::MkvReader * reader,
    const unique_ptr<mkvparser::Segment> & parser_segment)
{
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
    throw runtime_error("WriteID failed while writing Segment header");
  }

  if (SerializeInt(writer, mkvmuxer::kEbmlUnknownValue, 8)) {
    throw runtime_error("SerializeInt failed while writing Segment header");
  }

  /* write Segment Info with no duration in particular */
  auto muxer_info = muxer_segment->GetSegmentInfo();
  muxer_info->set_timecode_scale(parser_info->GetTimeCodeScale());
  muxer_info->Write(writer);

  /* simply copy Tracks element */
  long long tracks_start = parser_tracks->m_element_start;
  long tracks_size = narrow_cast<long>(parser_tracks->m_element_size);
  auto tracks_buffer = make_unique<unsigned char[]>(tracks_size);

  if (reader->Read(tracks_start, tracks_size, tracks_buffer.get())) {
    throw runtime_error("failed to read (copy) Tracks element");
  }

  if (writer->Write(tracks_buffer.get(), tracks_size)) {
    throw runtime_error("failed to write (forward) Tracks element");
  }

  /* copy all tags but the one with DURATION as TagName */
  auto muxer_tags = make_unique<mkvmuxer::Tags>();

  for (int i = 0; i < parser_tags->GetTagCount(); ++i) {
    auto parser_tag = parser_tags->GetTag(i);

    vector<pair<const char *, const char *>> tag_list;

    for (int j = 0; j < parser_tag->GetSimpleTagCount(); ++j) {
      auto parser_simple_tag = parser_tag->GetSimpleTag(j);

      auto tag_name = parser_simple_tag->GetTagName();
      if (strcmp(tag_name, "DURATION") == 0) {
        continue;
      }

      auto tag_string = parser_simple_tag->GetTagString();
      tag_list.emplace_back(make_pair(tag_name, tag_string));
    }

    if (tag_list.size()) {
      auto muxer_tag = muxer_tags->AddTag();

      for (const auto & item : tag_list) {
        muxer_tag->add_simple_tag(item.first, item.second);
      }
    }
  }

  muxer_tags->Write(writer);
}

void create_media_segment(
    mkvmuxer::MkvWriter * writer, mkvparser::MkvReader * reader,
    const unique_ptr<mkvparser::Segment> & parser_segment)
{
  if (parser_segment->GetCount() != 1) {
    throw runtime_error("input WebM should contain a single Cluster element");
  }

  /* copy Cluster except BlockGroup; assume it's at the end if there's one */
  auto cluster = parser_segment->GetFirst();
  if (not cluster) {
    throw runtime_error("no Cluster element is found");
  }
  /* set the time code */
  const mkvparser::BlockEntry * block_entry;
  if (cluster->GetFirst(block_entry)) {
    throw runtime_error("failed to get the first block of cluster");
  }

  const mkvparser::BlockEntry * prev_block_entry = nullptr;

  while (block_entry and not block_entry->EOS()) {
    if (block_entry->GetKind() != mkvparser::BlockEntry::kBlockSimple) {
      break;
    }

    prev_block_entry = block_entry;
    if (cluster->GetNext(block_entry, block_entry)) {
      throw runtime_error("failed to get the next block of cluster");
    }
  }

  if (prev_block_entry == nullptr) {
    throw runtime_error("no SimpleBlock exists");
  }

  /* last SimpleBlock */
  auto prev_block = prev_block_entry->GetBlock();

  /* get new payload size of Cluster (BlockGroup may have been removed) */
  long long cluster_start = cluster->m_element_start;
  long new_cluster_size = narrow_cast<long>(prev_block->m_start +
                              prev_block->m_size - cluster_start);
  const int header_size = 12; /* ID (4) + size (8) */
  long payload_size = new_cluster_size - header_size;

  /* manually write Cluster header with the correct payload size */
  if (WriteID(writer, libwebm::kMkvCluster)) {
    throw runtime_error("WriteID failed while writing Cluster header");
  }

  if (WriteUIntSize(writer, payload_size, 8)) {
    throw runtime_error("SerializeInt failed while writing Cluster header");
  }

  /* copy the payload of Cluster element */
  auto cluster_buffer = make_unique<unsigned char[]>(payload_size);

  if (reader->Read(cluster_start + header_size, payload_size,
                   cluster_buffer.get())) {
    throw runtime_error("failed to read (copy) Cluster element");
  }

  if (writer->Write(cluster_buffer.get(), payload_size)) {
    throw runtime_error("failed to write (forward) Cluster element");
  }
}

void fragment(const string & input_webm,
              const string & init_segment,
              const string & media_segment)
{
  mkvparser::MkvReader reader;
  if (reader.Open(input_webm.c_str())) {
    throw runtime_error("error while opening " + input_webm);
  }

  mkvmuxer::MkvWriter init_writer;
  if (init_segment.size()) {
    if (not init_writer.Open(init_segment.c_str())) {
      throw runtime_error("error while opening " + init_segment);
    }
  }

  mkvmuxer::MkvWriter media_writer;
  if (media_segment.size()) {
    if (not media_writer.Open(media_segment.c_str())) {
      throw runtime_error("error while opening " + media_segment);
    }
  }

  long long pos = 0;

  /* parse EBML header */
  mkvparser::EBMLHeader ebml_header;
  long long ret = ebml_header.Parse(&reader, pos);
  if (ret) {
    throw runtime_error("EBMLHeader::Parse() failed");
  }

  /* write EBML header in init segment */
  if (init_segment.size()) {
    mkvmuxer::WriteEbmlHeader(&init_writer, ebml_header.m_docTypeVersion,
                              ebml_header.m_docType);
  }

  /* parse Segment element */
  mkvparser::Segment * parser_segment_raw;
  ret = mkvparser::Segment::CreateInstance(&reader, pos, parser_segment_raw);
  if (ret) {
    throw runtime_error("Segment::CreateInstance() failed");
  }

  std::unique_ptr<mkvparser::Segment> parser_segment(parser_segment_raw);
  ret = parser_segment->Load();
  if (ret < 0) {
    throw runtime_error("Segment::Load() failed");
  }

  /* write the rest of init segment */
  if (init_segment.size()) {
    create_init_segment(&init_writer, &reader, parser_segment);
  }

  /* write media segment */
  if (media_segment.size()) {
    create_media_segment(&media_writer, &reader, parser_segment);
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
