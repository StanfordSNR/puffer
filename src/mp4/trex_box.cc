#include <iostream>

#include "trex_box.hh"

using namespace std;
using namespace MP4;

TrexBox::TrexBox(const uint64_t size, const string & type)
  : FullBox(size, type), track_id_(), default_sample_description_index_(),
    default_sample_duration_(), default_sample_size_(), default_sample_flags_()
{}

TrexBox::TrexBox(const string & type,
                 const uint8_t version,
                 const uint32_t flags,
                 const uint32_t track_id,
                 const uint32_t default_sample_description_index,
                 const uint32_t default_sample_duration,
                 const uint32_t default_sample_size,
                 const uint32_t default_sample_flags)
  : FullBox(type, version, flags), track_id_(track_id),
    default_sample_description_index_(default_sample_description_index),
    default_sample_duration_(default_sample_duration),
    default_sample_size_(default_sample_size),
    default_sample_flags_(default_sample_flags)
{}

void TrexBox::print_box(const unsigned int indent)
{
  print_size_type(indent);

  string indent_str = string(indent + 2, ' ') + "| ";
  cout << indent_str << "track id " << track_id_ << endl;
}

void TrexBox::parse_data(MP4File & mp4, const uint64_t data_size)
{
  uint64_t init_offset = mp4.curr_offset();

  parse_version_flags(mp4);

  track_id_ = mp4.read_uint32();
  default_sample_description_index_ = mp4.read_uint32();
  default_sample_duration_ = mp4.read_uint32();
  default_sample_size_ = mp4.read_uint32();
  default_sample_flags_ = mp4.read_uint32();

  skip_data_left(mp4, init_offset, data_size);
}

void TrexBox::write_box(MP4File & mp4)
{
  uint64_t size_offset = mp4.curr_offset();

  write_size_type(mp4);
  write_version_flags(mp4);

  mp4.write_uint32(track_id_);
  mp4.write_uint32(default_sample_description_index_);
  mp4.write_uint32(default_sample_duration_);
  mp4.write_uint32(default_sample_size_);
  mp4.write_uint32(default_sample_flags_);

  fix_size_at(mp4, size_offset);
}
