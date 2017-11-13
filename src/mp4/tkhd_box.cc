#include <iostream>

#include "tkhd_box.hh"

using namespace std;
using namespace MP4;

TkhdBox::TkhdBox(const uint64_t size, const string & type)
  : FullBox(size, type), creation_time_(), modification_time_(),
    track_id_(), duration_(), volume_(), width_(), height_()
{}

TkhdBox::TkhdBox(const string & type,
                 const uint8_t version,
                 const uint32_t flags,
                 const uint64_t creation_time,
                 const uint64_t modification_time,
                 const uint32_t track_id,
                 const uint64_t duration,
                 const int16_t volume,
                 const uint32_t width,
                 const uint32_t height,
                 const int16_t layer,
                 const int16_t alternate_group,
                 const vector<int32_t> matrix)
  : FullBox(type, version, flags), creation_time_(creation_time),
    modification_time_(modification_time), track_id_(track_id),
    duration_(duration), volume_(volume), width_(width), height_(height),
    layer_(layer), alternate_group_(alternate_group), matrix_(matrix)
{}

void TkhdBox::print_box(const unsigned int indent)
{
  print_size_type(indent);

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
    mp4.read(4); /* reserved */
    duration_ = mp4.read_uint64();
  } else {
    creation_time_ = mp4.read_uint32();
    modification_time_ = mp4.read_uint32();
    track_id_ = mp4.read_uint32();
    mp4.read(4); /* reserved */
    duration_ = mp4.read_uint32();
  }

  mp4.read(8); /* reserved */
  layer_ = mp4.read_int16();
  alternate_group_ = mp4.read_int16();
  volume_ = mp4.read_int16();
  mp4.read(2); /* reserved */

  vector<int32_t> matrix;
  for (int i = 0; i < 9; ++i) {
    matrix.emplace_back(mp4.read_int32());
  }
  matrix_ = move(matrix);

  /* width and height are 16.16 fixed-point numbers */
  width_ = mp4.read_uint32() / 65536;
  height_ = mp4.read_uint32() / 65536;

  check_data_left(mp4, data_size, init_offset);
}

void TkhdBox::write_box(MP4File & mp4)
{
  uint64_t size_offset = mp4.curr_offset();

  write_size_type(mp4);
  write_version_flags(mp4);

  if (version() == 1) {
    mp4.write_uint64(creation_time_);
    mp4.write_uint64(modification_time_);
    mp4.write_uint32(track_id_);
    mp4.write_zeros(4); /* reserved */
    mp4.write_uint64(duration_);
  } else {
    mp4.write_uint32(creation_time_);
    mp4.write_uint32(modification_time_);
    mp4.write_uint32(track_id_);
    mp4.write_zeros(4); /* reserved */
    mp4.write_uint32(duration_);
  }

  mp4.write_zeros(8); /* reserved */
  mp4.write_int16(layer_);
  mp4.write_int16(alternate_group_);
  mp4.write_int16(volume_);
  mp4.write_zeros(2); /* reserved */

  for (const auto & element : matrix_) {
    mp4.write_int32(element);
  }

  /* width and height are 16.16 fixed-point numbers */
  mp4.write_uint32(width_ * 65536);
  mp4.write_uint32(height_ * 65536);

  fix_size_at(mp4, size_offset);
}
