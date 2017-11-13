#ifndef TKHD_BOX_HH
#define TKHD_BOX_HH

#include <string>
#include <vector>

#include "box.hh"

namespace MP4 {

class TkhdBox : public FullBox
{
public:
  TkhdBox(const uint64_t size, const std::string & type);
  TkhdBox(const std::string & type,
          const uint8_t version,
          const uint32_t flags,
          const uint64_t creation_time,
          const uint64_t modification_time,
          const uint32_t track_id,
          const uint64_t duration,
          const int16_t volume,
          const uint32_t width,
          const uint32_t height,
          const int16_t layer = 0,
          const int16_t alternate_group = 0,
          const std::vector<int32_t> matrix = {
            0x00010000, 0, 0, 0, 0x00010000, 0, 0, 0, 0x40000000
          });

  /* accessors */
  uint64_t creation_time() { return creation_time_; }
  uint64_t modification_time() { return modification_time_; }
  uint32_t track_id() { return track_id_; }
  uint64_t duration() { return duration_; }
  uint32_t width() { return width_; }
  uint32_t height() { return height_; }

  /* mutators */
  void set_duration(const uint64_t duration) { duration_ = duration; }

  void print_box(const unsigned int indent = 0);

  void parse_data(MP4File & mp4, const uint64_t data_size);
  void write_box(MP4File & mp4);

private:
  uint64_t creation_time_;
  uint64_t modification_time_;
  uint32_t track_id_;
  uint64_t duration_;
  int16_t volume_;
  uint32_t width_;
  uint32_t height_;
  int16_t layer_ = 0;
  int16_t alternate_group_ = 0;
  std::vector<int32_t> matrix_ = {
    0x00010000, 0, 0, 0, 0x00010000, 0, 0, 0, 0x40000000
  };
};

} /* namespace MP4 */

#endif /* TKHD_BOX_HH */
