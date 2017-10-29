#include <iostream>

#include "tkhd_box.hh"

using namespace std;
using namespace MP4;

TkhdBox::TkhdBox(const uint64_t size, const std::string & type)
  : FullBox(size, type), creation_time_(), modification_time_(),
    track_ID_(), duration_()
{}

void TkhdBox::print_structure(const unsigned int indent)
{
  cout << string(indent, ' ') << "- " << type() << " " << size() << endl;

  string indent_str = string(indent + 2, ' ') + "| ";
  cout << indent_str << "track ID " << track_ID_ << endl;
  cout << indent_str << "duration " << duration_ << endl;
}

void TkhdBox::parse_data(MP4File & mp4, const uint64_t data_size)
{
  uint64_t init_offset = mp4.curr_offset();

  FullBox::parse_data(mp4);

  if (version() == 1) {
    creation_time_ = mp4.read_uint64();
    modification_time_ = mp4.read_uint64();
    track_ID_ = mp4.read_uint32();
    mp4.read_uint32(); /* reserved */
    duration_ = mp4.read_uint64();
  } else {
    creation_time_ = mp4.read_uint32();
    modification_time_ = mp4.read_uint32();
    track_ID_ = mp4.read_uint32();
    mp4.read_uint32(); /* reserved */
    duration_ = mp4.read_uint32();
  }

  skip_data_left(mp4, data_size, init_offset);
}
