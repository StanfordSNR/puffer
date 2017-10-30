#include <iostream>

#include "tkhd_box.hh"

using namespace std;
using namespace MP4;

TkhdBox::TkhdBox(const uint64_t size, const string & type)
  : FullBox(size, type), creation_time_(), modification_time_(),
    track_id_(), duration_(), width_(), height_()
{}

void TkhdBox::print_structure(const unsigned int indent)
{
  print_type_size(indent);

  string indent_str = string(indent + 2, ' ') + "| ";
  cout << indent_str << "track id " << track_id_ << endl;
  cout << indent_str << "duration " << duration_ << endl;

  if (width_ > 0 and height_ > 0) {
    cout << indent_str << "width " << width_ << endl;
    cout << indent_str << "height " << height_ << endl;
  }
}

void TkhdBox::parse_data(MP4File & mp4, const uint64_t data_size)
{
  uint64_t init_offset = mp4.curr_offset();

  parse_version_flags(mp4);

  if (version() == 1) {
    creation_time_ = mp4.read_uint64();
    modification_time_ = mp4.read_uint64();
    track_id_ = mp4.read_uint32();
    mp4.read_zeros(4); /* reserved */
    duration_ = mp4.read_uint64();
  } else {
    creation_time_ = mp4.read_uint32();
    modification_time_ = mp4.read_uint32();
    track_id_ = mp4.read_uint32();
    mp4.read_zeros(4); /* reserved */
    duration_ = mp4.read_uint32();
  }

  mp4.read_zeros(8); /* reserved */
  mp4.read_zeros(2); /* layer */
  mp4.read_zeros(2); /* alternate_group */
  mp4.read(2);       /* volume */
  mp4.read_zeros(2); /* reserved */
  mp4.read(36);      /* matrix */

  /* width and height are 16.16 fixed-point numbers */
  width_ = mp4.read_uint32() / 65536;
  height_ = mp4.read_uint32() / 65536;

  check_data_left(mp4, data_size, init_offset);
}
