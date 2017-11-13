#ifndef TRUN_BOX_HH
#define TRUN_BOX_HH

#include <string>
#include <vector>

#include "box.hh"

namespace MP4 {

class TrunBox : public FullBox
{
public:
  struct Sample {
    uint32_t sample_duration;
    uint32_t sample_size;
    uint32_t sample_flags;
    /* use int64_t to hold both unsigned and signed int32 */
    int64_t sample_composition_time_offset;
  };

  TrunBox(const uint64_t size, const std::string & type);
  TrunBox(const std::string & type,
          const uint8_t version,
          const uint32_t flags,
          std::vector<Sample> samples,
          const int32_t data_offset = 0,
          const uint32_t first_sample_flags = 0);

  /* accessors */
  uint32_t sample_count() { return samples_.size(); }
  std::vector<Sample> samples() { return samples_; }

  unsigned int data_offset_pos() {
    return FullBox::header_size() + 4 /* sample_count */;
  }

  uint64_t total_sample_duration();
  uint64_t total_sample_size();

  /* mutators */
  void set_data_offset(const int32_t data_offset) {
    data_offset_ = data_offset;
  }

  void print_box(const unsigned int indent = 0);

  void parse_data(MP4File & mp4, const uint64_t data_size);
  void write_box(MP4File & mp4);

  static const uint32_t data_offset_present = 0x000001;
  static const uint32_t first_sample_flags_present = 0x000004;
  static const uint32_t sample_duration_present = 0x000100;
  static const uint32_t sample_size_present = 0x000200;
  static const uint32_t sample_flags_present = 0x000400;
  static const uint32_t sample_composition_time_offsets_present = 0x000800;

private:
  std::vector<Sample> samples_;

  int32_t data_offset_ = 0;
  uint32_t first_sample_flags_ = 0;
};

} /* namespace MP4 */

#endif /* TRUN_BOX_HH */
