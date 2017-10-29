#include <iostream>

#include "mfhd_box.hh"

using namespace std;
using namespace MP4;

MfhdBox::MfhdBox(const uint64_t size, const std::string & type)
  : FullBox(size, type), sequence_number_()
{}

void MfhdBox::print_structure(const unsigned int indent)
{
  cout << string(indent, ' ') << "- " << type() << " " << size() << endl;

  string indent_str = string(indent + 2, ' ') + "| ";
  cout << indent_str << "sequence number " << sequence_number_ << endl;
}

void MfhdBox::parse_data(MP4File & mp4, const uint64_t data_size)
{
  uint64_t init_offset = mp4.curr_offset();

  FullBox::parse_data(mp4);

  sequence_number_ = mp4.read_uint32();

  check_data_left(mp4, init_offset, data_size);
}
