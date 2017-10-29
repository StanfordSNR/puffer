#include <iostream>

#include "stsz_box.hh"

using namespace std;
using namespace MP4;

StszBox::StszBox(const uint64_t size, const string & type)
  : Box(size, type), version_(), flags_(), sample_size_(), sample_count_(),
  entries_()
{}

void StszBox::parse_data(MP4File & mp4, const uint64_t data_size)
{
  int64_t pos = mp4.curr_offset();
  tie(version_, flags_) = mp4.read_version_flags();
  sample_size_ = mp4.read_uint32();
  sample_count_ = mp4.read_uint32();

  if (sample_size_ == 0) {
    for (uint32_t i = 0; i < sample_count_; i++)
      entries_.emplace_back(mp4.read_uint32());
  }
  if (mp4.curr_offset() - pos != data_size)
    throw runtime_error("Invalid stsz box");
}

void StszBox::print_structure(const unsigned int indent)
{
  cout << string(indent, ' ') << "- " << type() << " " << size() << endl;

  string indent_str = string(indent + 2, ' ') + "| ";

  cout << indent_str << "sample_count " << sample_count_ << endl;

  indent_str = string(indent + 4, ' ') + "|- ";
  for (auto entry: entries_)
    cout << indent_str << entry << endl;
}
