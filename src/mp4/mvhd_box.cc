#include <iostream>

#include "mvhd_box.hh"

using namespace std;
using namespace MP4;

MvhdBox::MvhdBox(const uint64_t size, const string & type)
  : FullBox(size, type), creation_time_(), modification_time_(),
    timescale_(), duration_(), next_track_id_()
{}

MvhdBox::MvhdBox(const string & type,
                 const uint8_t version,
                 const uint32_t flags,
                 const uint64_t creation_time,
                 const uint64_t modification_time,
                 const uint32_t timescale,
                 const uint64_t duration,
                 const uint32_t next_track_id,
                 const int32_t rate,
                 const int16_t volume,
                 const vector<int32_t> matrix)
  : FullBox(type, version, flags), creation_time_(creation_time),
    modification_time_(modification_time), timescale_(timescale),
    duration_(duration), next_track_id_(next_track_id),
    rate_(rate), volume_(volume), matrix_(matrix)
{}

void MvhdBox::print_box(const unsigned int indent)
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

  rate_ = mp4.read_int32();
  volume_ = mp4.read_int16();

  mp4.read(2); /* reserved */
  mp4.read(8); /* reserved */

  vector<int32_t> matrix;
  for (int i = 0; i < 9; ++i) {
    matrix.emplace_back(mp4.read_int32());
  }
  matrix_ = move(matrix);

  mp4.read(24); /* pre-defined */

  next_track_id_ = mp4.read_uint32();

  check_data_left(mp4, data_size, init_offset);
}

void MvhdBox::write_box(MP4File & mp4)
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

  mp4.write_int32(rate_);
  mp4.write_int16(volume_);

  mp4.write_zeros(2); /* reserved */
  mp4.write_zeros(8); /* reserved */

  for (const auto & element : matrix_) {
    mp4.write_int32(element);
  }

  mp4.write_zeros(24); /* pre-defined */

  mp4.write_uint32(next_track_id_);

  fix_size_at(mp4, size_offset);
}
