#include <iostream>

#include "stsz_box.hh"

using namespace std;
using namespace MP4;

StszBox::StszBox(const uint64_t size, const string & type)
  : FullBox(size, type), sample_size_(), sample_count_(), entries_()
{}

void StszBox::print_structure(const unsigned int indent)
{
  print_type_size(indent);

  string indent_str = string(indent + 2, ' ') + "| ";
  cout << indent_str << "sample count " << sample_count_ << endl;
}

void StszBox::parse_data(MP4File & mp4, const uint64_t data_size)
{
  uint64_t init_offset = mp4.curr_offset();

  parse_version_flags(mp4);

  sample_size_ = mp4.read_uint32();
  sample_count_ = mp4.read_uint32();

  if (sample_size_ == 0) {
    for (uint32_t i = 0; i < sample_count_; ++i) {
      entries_.emplace_back(mp4.read_uint32());
    }
  }

  check_data_left(mp4, data_size, init_offset);
}
