#include <iostream>

#include "stsd_box.hh"

using namespace std;
using namespace MP4;

StsdBox::StsdBox(const uint64_t size, const string & type)
  : Box(size, type), version_(), flags_()
{}

void StsdBox::parse_data(MP4File & mp4, const uint64_t data_size)
{
  uint64_t init_offset = mp4.curr_offset();

  tie(version_, flags_) = mp4.read_version_flags();

  /* parse sample entries */
  uint32_t num_sample_entries = mp4.read_uint32();
  for (uint32_t i = 0; i < num_sample_entries; ++i) {
    uint32_t sample_size = mp4.read_uint32();
    string sample_type = mp4.read(4);

    shared_ptr<Box> box;
    if (sample_type == "avc1") {
      box = make_shared<AVC1>(sample_size, sample_type);
    } else {
      box = make_shared<Box>(sample_size, sample_type);
    }

    uint32_t sample_data_size = sample_size - 8;
    uint64_t sample_init_offset = mp4.curr_offset();

    box->parse_data(mp4, sample_data_size);
    add_child(move(box));

    skip_data(mp4, sample_data_size, sample_init_offset);
  }

  skip_data(mp4, data_size, init_offset);
}

SampleEntry::SampleEntry(const uint64_t size, const std::string & type)
  : Box(size, type), data_reference_index_()
{}

void SampleEntry::parse_data(MP4File & mp4)
{
  /* 6 bytes of 0 (reserved) */
  uint32_t reserved1 = mp4.read_uint32();
  uint16_t reserved2 = mp4.read_uint16();
  if (reserved1 != 0 or reserved2 != 0) {
    throw std::runtime_error("Invalid SampleEntry data");
  }

  data_reference_index_ = mp4.read_uint16();
}

VisualSampleEntry::VisualSampleEntry(const uint64_t size, const string & type)
  : SampleEntry(size, type), width_(), height_(), compressorname_()
{}

void VisualSampleEntry::parse_data(MP4File & mp4)
{
  SampleEntry::parse_data(mp4);

  /* 16 bytes of 0 (pre_defined and reserved) */
  uint64_t reserved1 = mp4.read_uint64();
  uint64_t reserved2 = mp4.read_uint64();
  if (reserved1 != 0 or reserved2 != 0) {
    throw runtime_error("Invalid VisualSampleEntry");
  }

  width_ = mp4.read_uint16();
  height_ = mp4.read_uint16();
  horizresolution_ = mp4.read_uint32();
  vertresolution_ = mp4.read_uint32();

  /* reserved */
  mp4.read_uint32();
  frame_count_ = mp4.read_uint16();
  uint8_t count = mp4.read_uint8();
  compressorname_ = mp4.read(32 - count - 1);
  depth_ = mp4.read_uint16();

  pre_defined_ = mp4.read_uint16();
}

AVC1::AVC1(const uint64_t size, const string & type)
  : VisualSampleEntry(size, type), avc_profile_(),
    avc_profile_compatibility_(), avc_level_(), avcc_size_()
{}

void AVC1::parse_data(MP4File & mp4, const uint64_t data_size)
{
  uint64_t init_offset = mp4.curr_offset();

  VisualSampleEntry::parse_data(mp4);

  /* avcc and avc1 are parsed together */
  avcc_size_ = mp4.read_uint32();
  string avcc = mp4.read(4);

  if (avcc != "avcC") {
    throw runtime_error("AVCC does not follow AVC1 immediately");
  }

  configuration_version_ = mp4.read_uint8();
  avc_profile_ = mp4.read_uint8();
  avc_profile_compatibility_ = mp4.read_uint8();
  avc_level_ = mp4.read_uint8();

  skip_data(mp4, data_size, init_offset);
}

void AVC1::print_structure(const unsigned int indent)
{
  cout << string(indent, ' ') << "- " << type() << " " << size() << endl;

  string indent_str = string(indent + 2, ' ') + "| ";
  cout << indent_str << "width " << width() << endl;
  cout << indent_str << "height " << height() << endl;

  cout << string(indent + 2, ' ') << "- " << "avcC" << " " <<
          avcc_size_ << endl;

  indent_str = string(indent + 4, ' ') + "| ";
  cout << indent_str << "avc_profile " << unsigned(avc_profile_) << endl;
  cout << indent_str << "avc_level " << unsigned(avc_level_) << endl;
}
