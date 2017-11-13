#ifndef MVHD_BOX_HH
#define MVHD_BOX_HH

#include <string>
#include <vector>

#include "box.hh"

namespace MP4 {

class MvhdBox : public FullBox
{
public:
  MvhdBox(const uint64_t size, const std::string & type);
  MvhdBox(const std::string & type,
          const uint8_t version,
          const uint32_t flags,
          const uint64_t creation_time,
          const uint64_t modification_time,
          const uint32_t timescale,
          const uint64_t duration,
          const uint32_t next_track_id,
          const int32_t rate = 0x00010000,
          const int16_t volume = 0x0100,
          const std::vector<int32_t> matrix = {
            0x00010000, 0, 0, 0, 0x00010000, 0, 0, 0, 0x40000000
          });


  /* accessors */
  uint64_t creation_time() { return creation_time_; }
  uint64_t modificaiton_time() { return modification_time_; }
  uint32_t timescale() { return timescale_; }
  uint64_t duration() { return duration_; }

  /* mutators */
  void set_duration(const uint64_t duration) { duration_ = duration; }

  void print_box(const unsigned int indent = 0);

  void parse_data(MP4File & mp4, const uint64_t data_size);
  void write_box(MP4File & mp4);

private:
  uint64_t creation_time_;
  uint64_t modification_time_;
  uint32_t timescale_;
  uint64_t duration_;
  uint32_t next_track_id_;
  int32_t rate_ = 0x00010000;
  int16_t volume_ = 0x0100;
  std::vector<int32_t> matrix_ = {
      0x00010000, 0, 0, 0, 0x00010000, 0, 0, 0, 0x40000000
  };
};

} /* namespace MP4 */

#endif /* MVHD_BOX_HH */
