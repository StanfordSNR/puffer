#include <iostream>

#include "trun_box.hh"

using namespace std;
using namespace MP4;

const uint32_t data_offset_present = 0x000001;
const uint32_t first_sample_flags_present = 0x000004;
const uint32_t sample_duration_present = 0x000100;
const uint32_t sample_size_present = 0x000200;
const uint32_t sample_flags_present = 0x000400;
const uint32_t sample_composition_time_offsets_present = 0x000800;

TrunBox::TrunBox(const uint64_t size, const string & type)
  : Box(size, type), version_(), flags_(), sample_count_(), duration_(0)
{}

void TrunBox::parse_data(MP4File & mp4, const uint64_t data_size)
{
  int64_t pos = mp4.curr_offset();
  tie(version_, flags_) = mp4.read_version_flags();
  sample_count_ = mp4.read_uint32();
 
  if (flags_ & data_offset_present)
    mp4.read_uint32();

  if (flags_ & first_sample_flags_present)
    mp4.read_uint32();

  for(uint32_t i = 0; i < sample_count_; i++) {
    if(flags_ & sample_duration_present)
      duration_ += mp4.read_uint32();

    if(flags_ & sample_size_present)
      mp4.read_uint32();

    if(flags_ & sample_flags_present)
      mp4.read_uint32();

    if(flags_ & sample_composition_time_offsets_present)
      mp4.read_uint32();
  }

  if (mp4.curr_offset() - pos != data_size)
    throw runtime_error("Invalid trun box");
}

void TrunBox::print_structure(const unsigned int indent)
{
  cout << string(indent, ' ') << "- " << type() << " " << size() << endl;

  string indent_str = string(indent + 2, ' ') + "| ";

  cout << indent_str << "sample_count " << sample_count_ << endl;
  if (duration_ > 0)
    cout << indent_str << "duration " << duration_ << endl;
}
