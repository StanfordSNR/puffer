#include <iostream>

#include "tfdt_box.hh"
#include "strict_conversions.hh"

using namespace std;
using namespace MP4;

TfdtBox::TfdtBox(const uint64_t size, const string & type)
  : FullBox(size, type), base_media_decode_time_()
{}

TfdtBox::TfdtBox(const string & type,
                 const uint8_t version,
                 const uint32_t flags,
                 const uint64_t base_media_decode_time)
  : FullBox(type, version, flags),
    base_media_decode_time_(base_media_decode_time)
{}

void TfdtBox::print_box(const unsigned int indent)
{
  print_size_type(indent);

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

void TfdtBox::write_box(MP4File & mp4)
{
  uint64_t size_offset = mp4.curr_offset();

  write_size_type(mp4);
  write_version_flags(mp4);

  if (version() == 1) {
    mp4.write_uint64(base_media_decode_time_);
  } else {
    mp4.write_uint32(narrow_cast<uint32_t>(base_media_decode_time_));
  }

  fix_size_at(mp4, size_offset);
}
