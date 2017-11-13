#ifndef TFHD_BOX_HH
#define TFHD_BOX_HH

#include <string>

#include "box.hh"

namespace MP4 {

class TfhdBox : public FullBox
{
public:
  TfhdBox(const uint64_t size, const std::string & type);
  TfhdBox(const std::string & type,
          const uint8_t version,
          const uint32_t flags,
          const uint32_t track_id,
          const uint32_t default_sample_duration = 0,
          const uint32_t default_sample_size = 0,
          const uint32_t default_sample_flags = 0,
          const uint64_t base_data_offset = 0,
          const uint32_t sample_description_index = 0);

  /* accessors */
  uint32_t track_id() { return track_id_; }
  uint64_t base_data_offset() { return base_data_offset_; }
  uint32_t sample_description_index() { return sample_description_index_; }
  uint32_t default_sample_duration() { return default_sample_duration_; }
  uint32_t default_sample_size() { return default_sample_size_; }
  uint32_t default_sample_flags() { return default_sample_flags_; }

  void print_box(const unsigned int indent = 0);

  void parse_data(MP4File & mp4, const uint64_t data_size);
  void write_box(MP4File & mp4);

  static const uint32_t base_data_offset_present = 0x000001;
  static const uint32_t sample_description_index_present = 0x000002;
  static const uint32_t default_sample_duration_present = 0x000008;
  static const uint32_t default_sample_size_present = 0x000010;
  static const uint32_t default_sample_flags_present = 0x000020;
  static const uint32_t default_base_is_moof = 0x020000;

private:
  uint32_t track_id_;
  uint64_t base_data_offset_ = 0;
  uint32_t sample_description_index_ = 0;
  uint32_t default_sample_duration_ = 0;
  uint32_t default_sample_size_ = 0;
  uint32_t default_sample_flags_ = 0;
};

} /* namespace MP4 */

#endif /* TFHD_BOX_HH */
