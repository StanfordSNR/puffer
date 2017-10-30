#include <iostream>

#include "tfdt_box.hh"

using namespace std;
using namespace MP4;

TfdtBox::TfdtBox(const uint64_t size, const std::string & type)
  : FullBox(size, type), base_media_decode_time_()
{}

void TfdtBox::print_structure(const unsigned int indent)
{
  print_type_size(indent);

  string indent_str = string(indent + 2, ' ') + "| ";
  cout << indent_str << "decode time " << base_media_decode_time_ << endl;
}

void TfdtBox::parse_data(MP4File & mp4, const uint64_t data_size)
{
  uint64_t init_offset = mp4.curr_offset();

  parse_version_flags(mp4);

  if (version() == 1) {
    base_media_decode_time_ = mp4.read_uint64();
  } else {
    base_media_decode_time_ = mp4.read_uint32();
  }

  check_data_left(mp4, init_offset, data_size);
}
