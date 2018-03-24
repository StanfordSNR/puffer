#include <iostream>
#include <stdexcept>

#include "sidx_box.hh"
#include "strict_conversions.hh"

using namespace std;
using namespace MP4;

SidxBox::SidxBox(const uint64_t size, const string & type)
  : FullBox(size, type), reference_id_(), timescale_(),
    earlist_presentation_time_(), first_offset_(), reference_list_()
{}

SidxBox::SidxBox(const string & type,
                 const uint8_t version,
                 const uint32_t flags,
                 const uint32_t reference_id,
                 const uint32_t timescale,
                 const uint64_t earlist_presentation_time,
                 const uint64_t first_offset,
                 const vector<SidxReference> & reference_list)
  : FullBox(type, version, flags),
    reference_id_(reference_id), timescale_(timescale),
    earlist_presentation_time_(earlist_presentation_time),
    first_offset_(first_offset), reference_list_(reference_list)
{}

unsigned int SidxBox::reference_list_pos()
{
  unsigned int pos = FullBox::header_size();

  pos += 8; /* reference_id and timescale */

  /* earlist_presentation_time and first_offset */
  if (version() == 0) {
    pos += 8;
  } else {
    pos += 16;
  }

  pos += 4; /* reserved and reference_count */

  return pos;
}

uint32_t SidxBox::duration()
{
  uint32_t duration = 0;

  for (const auto & ref : reference_list_) {
    duration += ref.subsegment_duration;
  }

  return duration;
}

void SidxBox::print_box(const unsigned int indent)
{
  print_size_type(indent);

  string indent_str = string(indent + 2, ' ') + "| ";
  cout << indent_str << "reference id " << reference_id_ << endl;
  cout << indent_str << "timescale " << timescale_ << endl;
  cout << indent_str << "earliest presentation time "
                     << earlist_presentation_time_ << endl;

  if (reference_count()) {
    cout << indent_str << "[#] referenced size, subsegment duration" << endl;
    for (unsigned int i = 0; i < reference_count() and i < 5; ++i) {
      const auto & ref = reference_list_[i];
      cout << indent_str << "[" << i << "] "
           << ref.referenced_size << ", " << ref.subsegment_duration << endl;
    }

    if (reference_count() > 5) {
      cout << indent_str << "..." << endl;
    }
  }
}

void SidxBox::parse_data(MP4File & mp4, const uint64_t data_size)
{
  uint64_t init_offset = mp4.curr_offset();

  parse_version_flags(mp4);

  reference_id_ = mp4.read_uint32();
  timescale_ = mp4.read_uint32();

  if (version() == 0) {
    earlist_presentation_time_ = mp4.read_uint32();
    first_offset_ = mp4.read_uint32();
  } else {
    earlist_presentation_time_ = mp4.read_uint64();
    first_offset_ = mp4.read_uint64();
  }

  mp4.read(2); /* reserved */

  uint16_t reference_count = mp4.read_uint16();
  for (unsigned int i = 0; i < reference_count; ++i) {
    uint32_t data = mp4.read_uint32();
    bool reference_type = (data >> 31) & 1;
    uint32_t referenced_size = data & 0x7FFFFFFF;

    uint32_t subsegment_duration = mp4.read_uint32();

    data = mp4.read_uint32();
    bool starts_with_sap = (data >> 31) & 1;
    uint8_t sap_type = (data >> 28) & 7;
    uint32_t sap_delta = data & 0x0FFFFFFF;

    reference_list_.push_back({
      reference_type, referenced_size, subsegment_duration,
      starts_with_sap, sap_type, sap_delta
    });
  }

  check_data_left(mp4, data_size, init_offset);
}

void SidxBox::write_box(MP4File & mp4)
{
  uint64_t size_offset = mp4.curr_offset();

  write_size_type(mp4);
  write_version_flags(mp4);

  mp4.write_uint32(reference_id_);
  mp4.write_uint32(timescale_);

  if (version() == 0) {
    mp4.write_uint32(narrow_cast<uint32_t>(earlist_presentation_time_));
    mp4.write_uint32(narrow_cast<uint32_t>(first_offset_));
  } else {
    mp4.write_uint64(earlist_presentation_time_);
    mp4.write_uint64(first_offset_);
  }

  mp4.write_zeros(2); /* reserved */

  mp4.write_uint16(reference_list_.size());
  for (const auto & ref : reference_list_) {
    uint32_t data = (ref.reference_type << 31) +
                    (ref.referenced_size & 0x7FFFFFFF);
    mp4.write_uint32(data);

    mp4.write_uint32(ref.subsegment_duration);

    data = (ref.starts_with_sap << 31) + (ref.sap_type << 28) +
           (ref.sap_delta & 0x0FFFFFFF);
    mp4.write_uint32(data);
  }

  fix_size_at(mp4, size_offset);
}
