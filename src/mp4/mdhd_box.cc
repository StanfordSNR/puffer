#include <iostream>

#include "mdhd_box.hh"

using namespace std;
using namespace MP4;

MdhdBox::MdhdBox(const uint64_t size, const std::string & type)
  : FullBox(size, type), creation_time_(), modification_time_(),
    timescale_(), duration_(), language_()
{}

void MdhdBox::print_structure(const unsigned int indent)
{
  cout << string(indent, ' ') << "- " << type() << " " << size() << endl;

  string indent_str = string(indent + 2, ' ') + "| ";
  cout << indent_str << "timescale " << timescale_ << endl;
  cout << indent_str << "duration " << duration_ << endl;
  cout << indent_str << "language " << language_ << endl;
}

void MdhdBox::parse_data(MP4File & mp4, const uint64_t data_size)
{
  uint64_t init_offset = mp4.curr_offset();

  FullBox::parse_data(mp4);

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

  uint32_t temp = mp4.read_uint32();
  language_ = (temp >> 16) & 0x7FFF;

  check_data_left(mp4, data_size, init_offset);
}
