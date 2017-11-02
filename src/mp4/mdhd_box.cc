#include <iostream>

#include "mdhd_box.hh"

using namespace std;
using namespace MP4;

MdhdBox::MdhdBox(const uint64_t size, const string & type)
  : FullBox(size, type), creation_time_(), modification_time_(),
    timescale_(), duration_(), language_()
{}

MdhdBox::MdhdBox(const string & type,
                 const uint8_t version,
                 const uint32_t flags,
                 const uint64_t creation_time,
                 const uint64_t modification_time,
                 const uint32_t timescale,
                 const uint64_t duration,
                 const uint16_t language)
  : FullBox(type, version, flags), creation_time_(creation_time),
    modification_time_(modification_time), timescale_(timescale),
    duration_(duration), language_(language)
{}

void MdhdBox::print_box(const unsigned int indent)
{
  print_size_type(indent);

  string indent_str = string(indent + 2, ' ') + "| ";
  cout << indent_str << "timescale " << timescale_ << endl;
  cout << indent_str << "duration " << duration_ << endl;
}

void MdhdBox::parse_data(MP4File & mp4, const uint64_t data_size)
{
  uint64_t init_offset = mp4.curr_offset();

  parse_version_flags(mp4);

  if (version() == 1) {
    creation_time_ = mp4.read_uint64();
    modification_time_ = mp4.read_uint64();
    timescale_ = mp4.read_uint32();
    duration_ = mp4.read_uint64();
  } else {
    creation_time_ = mp4.read_uint32();
    modification_time_ = mp4.read_uint32();
    timescale_ = mp4.read_uint32();
    duration_ = mp4.read_uint32();
  }

  uint16_t data = mp4.read_uint16();
  language_ = data & 0x7FFF;

  mp4.read(2); /* pre-defined */

  check_data_left(mp4, data_size, init_offset);
}

void MdhdBox::write_box(MP4File & mp4)
{
  uint64_t size_offset = mp4.curr_offset();

  write_size_type(mp4);
  write_version_flags(mp4);

  if (version() == 1) {
    mp4.write_uint64(creation_time_);
    mp4.write_uint64(modification_time_);
    mp4.write_uint32(timescale_);
    mp4.write_uint64(duration_);
  } else {
    mp4.write_uint32(creation_time_);
    mp4.write_uint32(modification_time_);
    mp4.write_uint32(timescale_);
    mp4.write_uint32(duration_);
  }

  mp4.write_uint16(language_ & 0x7FFF);
  mp4.write_zeros(2);

  fix_size_at(mp4, size_offset);
}
