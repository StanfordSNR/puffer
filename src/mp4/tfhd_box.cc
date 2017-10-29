#include <iostream>

#include "tfhd_box.hh"

using namespace std;
using namespace MP4;

const uint32_t base_data_offset_present = 0x000001;
const uint32_t sample_description_index_present = 0x000002;
const uint32_t default_sample_duration_present = 0x000008;
const uint32_t default_sample_size_present = 0x000010;
const uint32_t default_sample_flags_present = 0x000020;

TfhdBox::TfhdBox(const uint64_t size, const string & type)
  : Box(size, type), version_(), flags_(), track_ID_(), base_data_offset_(),
  sample_description_index_(), default_sample_duration_(),
  default_sample_size_(), default_sample_flags_()
{}

void TfhdBox::parse_data(MP4File & mp4, const uint64_t data_size)
{
  int64_t pos = mp4.curr_offset();
  tie(version_, flags_) = mp4.read_version_flags();
 
  track_ID_ = mp4.read_uint32();

  if (flags_ & base_data_offset_present)
    base_data_offset_ = mp4.read_uint32();

  if (flags_ & sample_description_index_present)
    sample_description_index_ = mp4.read_uint32();

  if (flags_ & default_sample_duration_present)
    default_sample_duration_ = mp4.read_uint32();

  if (flags_ & default_sample_size_present)
    default_sample_size_ = mp4.read_uint32();

  if (flags_ & default_sample_flags_present)
    default_sample_flags_ = mp4.read_uint32();

  if (mp4.curr_offset() - pos != data_size)
    throw runtime_error("Invalid trun box");
}

void TfhdBox::print_structure(const unsigned int indent)
{
  cout << string(indent, ' ') << "- " << type() << " " << size() << endl;

  if (default_sample_duration_ > 0) {
    string indent_str = string(indent + 2, ' ') + "| ";
    cout << indent_str << "sample default duration " << 
      default_sample_duration_ << endl;
  }
}
