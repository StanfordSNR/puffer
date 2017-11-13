#include <iostream>

#include "tfhd_box.hh"

using namespace std;
using namespace MP4;

TfhdBox::TfhdBox(const uint64_t size, const string & type)
  : FullBox(size, type), track_id_()
{}

TfhdBox::TfhdBox(const string & type,
                 const uint8_t version,
                 const uint32_t flags,
                 const uint32_t track_id,
                 const uint32_t default_sample_duration,
                 const uint32_t default_sample_size,
                 const uint32_t default_sample_flags,
                 const uint64_t base_data_offset,
                 const uint32_t sample_description_index)
  : FullBox(type, version, flags), track_id_(track_id)
{
  if (flags & base_data_offset_present) {
    base_data_offset_ = base_data_offset;
  }
  if (flags & sample_description_index_present) {
    sample_description_index_ = sample_description_index;
  }
  if (flags & default_sample_duration_present) {
    default_sample_duration_ = default_sample_duration;
  }
  if (flags & default_sample_size_present) {
    default_sample_size_ = default_sample_size;
  }
  if (flags & default_sample_flags_present) {
    default_sample_flags_ = default_sample_flags;
  }
}


void TfhdBox::print_box(const unsigned int indent)
{
  print_size_type(indent);

  string indent_str = string(indent + 2, ' ') + "| ";
  cout << indent_str << "track id " << track_id_ << endl;

  if (flags() & default_sample_duration_present) {
    cout << indent_str << "default sample duration "
         << default_sample_duration_ << endl;
  }
  if (flags() & default_sample_size_present) {
    cout << indent_str << "default sample size "
         << default_sample_size_ << endl;
  }
  if (flags() & default_sample_flags_present) {
    cout << indent_str << "default sample flags 0x"
         << hex << default_sample_flags_ << dec << endl;
  }
}

void TfhdBox::parse_data(MP4File & mp4, const uint64_t data_size)
{
  uint64_t init_offset = mp4.curr_offset();

  parse_version_flags(mp4);

  track_id_ = mp4.read_uint32();

  if (flags() & base_data_offset_present) {
    base_data_offset_ = mp4.read_uint64();
  }
  if (flags() & sample_description_index_present) {
    sample_description_index_ = mp4.read_uint32();
  }
  if (flags() & default_sample_duration_present) {
    default_sample_duration_ = mp4.read_uint32();
  }
  if (flags() & default_sample_size_present) {
    default_sample_size_ = mp4.read_uint32();
  }
  if (flags() & default_sample_flags_present) {
    default_sample_flags_ = mp4.read_uint32();
  }

  check_data_left(mp4, data_size, init_offset);
}

void TfhdBox::write_box(MP4File & mp4)
{
  uint64_t size_offset = mp4.curr_offset();

  write_size_type(mp4);
  write_version_flags(mp4);

  mp4.write_uint32(track_id_);

  if (flags() & base_data_offset_present) {
    mp4.write_uint64(base_data_offset_);
  }
  if (flags() & sample_description_index_present) {
    mp4.write_uint32(sample_description_index_);
  }
  if (flags() & default_sample_duration_present) {
    mp4.write_uint32(default_sample_duration_);
  }
  if (flags() & default_sample_size_present) {
    mp4.write_uint32(default_sample_size_);
  }
  if (flags() & default_sample_flags_present) {
    mp4.write_uint32(default_sample_flags_);
  }

  fix_size_at(mp4, size_offset);
}
