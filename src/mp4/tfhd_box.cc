#include <iostream>

#include "tfhd_box.hh"

using namespace std;
using namespace MP4;

TfhdBox::TfhdBox(const uint64_t size, const string & type)
  : FullBox(size, type), track_id_(), base_data_offset_(),
    sample_description_index_(), default_sample_duration_(),
    default_sample_size_(), default_sample_flags_()
{}

void TfhdBox::print_structure(const unsigned int indent)
{
  print_size_type(indent);

  string indent_str = string(indent + 2, ' ') + "| ";
  cout << indent_str << "track id " << track_id_ << endl;
  cout << indent_str << "sample default duration "
       << default_sample_duration_ << endl;
}

void TfhdBox::parse_data(MP4File & mp4, const uint64_t data_size)
{
  uint64_t init_offset = mp4.curr_offset();

  parse_version_flags(mp4);

  track_id_ = mp4.read_uint32();

  if (flags() & base_data_offset_present) {
    base_data_offset_ = mp4.read_uint32();
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
