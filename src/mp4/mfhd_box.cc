#include <iostream>

#include "mfhd_box.hh"

using namespace std;
using namespace MP4;

MfhdBox::MfhdBox(const uint64_t size, const string & type)
  : FullBox(size, type), sequence_number_()
{}

MfhdBox::MfhdBox(const string & type,
                 const uint8_t version,
                 const uint32_t flags,
                 const uint32_t sequence_number)
  : FullBox(type, version, flags), sequence_number_(sequence_number)
{}

void MfhdBox::print_box(const unsigned int indent)
{
  print_size_type(indent);

  string indent_str = string(indent + 2, ' ') + "| ";
  cout << indent_str << "sequence number " << sequence_number_ << endl;
}

void MfhdBox::parse_data(MP4File & mp4, const uint64_t data_size)
{
  uint64_t init_offset = mp4.curr_offset();

  parse_version_flags(mp4);

  sequence_number_ = mp4.read_uint32();

  check_data_left(mp4, init_offset, data_size);
}

void MfhdBox::write_box(MP4File & mp4)
{
  uint64_t size_offset = mp4.curr_offset();

  write_size_type(mp4);
  write_version_flags(mp4);
  mp4.write_uint32(sequence_number_);

  fix_size_at(mp4, size_offset);
}
