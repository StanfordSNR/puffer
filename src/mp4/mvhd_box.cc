#include <iostream>

#include "mvhd_box.hh"

using namespace std;
using namespace MP4;

MvhdBox::MvhdBox(const uint64_t size, const string & type)
  : FullBox(size, type), creation_time_(), modification_time_(),
    timescale_(), duration_()
{}

void MvhdBox::print_structure(const unsigned int indent)
{
  print_size_type(indent);

  string indent_str = string(indent + 2, ' ') + "| ";
  cout << indent_str << "timescale " << timescale_ << endl;
  cout << indent_str << "duration " << duration_ << endl;
}

void MvhdBox::parse_data(MP4File & mp4, const uint64_t data_size)
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

  skip_data_left(mp4, data_size, init_offset);
}
