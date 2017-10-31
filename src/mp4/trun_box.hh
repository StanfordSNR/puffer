#ifndef TRUN_BOX_HH
#define TRUN_BOX_HH

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

  /* accessors */
  uint32_t sample_count() { return samples_.size(); }
  std::vector<Sample> samples() { return samples_; }

  uint64_t total_sample_duration() { return total_sample_duration_; }
  uint64_t total_sample_size() { return total_sample_size_; }

  void print_structure(const unsigned int indent = 0);

  void parse_data(MP4File & mp4, const uint64_t data_size);

private:
  std::vector<Sample> samples_;

  uint64_t total_sample_duration_;
  uint64_t total_sample_size_;

  static const uint32_t data_offset_present = 0x000001;
  static const uint32_t first_sample_flags_present = 0x000004;
  static const uint32_t sample_duration_present = 0x000100;
  static const uint32_t sample_size_present = 0x000200;
  static const uint32_t sample_flags_present = 0x000400;
  static const uint32_t sample_composition_time_offsets_present = 0x000800;
};

}

#endif /* TRUN_BOX_HH */
