#include <iostream>

#include "tfhd_box.hh"

using namespace std;
using namespace MP4;

TfhdBox::TfhdBox(const uint64_t size, const std::string & type)
  : FullBox(size, type), track_id_()
{}

void TfhdBox::print_structure(const unsigned int indent)
{
  cout << string(indent, ' ') << "- " << type() << " " << size() << endl;

  string indent_str = string(indent + 2, ' ') + "| ";
  cout << indent_str << "track ID " << track_id_ << endl;
}

void TfhdBox::parse_data(MP4File & mp4, const uint64_t data_size)
{
  uint64_t init_offset = mp4.curr_offset();

  FullBox::parse_data(mp4);

  track_id_ = mp4.read_uint32();

  skip_data_left(mp4, init_offset, data_size);
}
