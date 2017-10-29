#ifndef TFHD_BOX_HH
#define TFHD_BOX_HH

#include "box.hh"

namespace MP4 {

class TfhdBox : public Box
{
public:
  TfhdBox(const uint64_t size, const std::string & type);

  /* accessors */
  uint8_t version() { return version_; }
  uint32_t flags() { return flags_; }
  uint32_t track_ID() { return track_ID_; }
  uint64_t base_data_offset() { return base_data_offset_; }
  uint32_t sample_description_index() { return sample_description_index_; }
  uint32_t default_sample_duration() { return default_sample_duration_; }
  uint32_t default_sample_size() { return default_sample_size_; }
  uint32_t default_sample_flags() { return default_sample_flags_; }

  void parse_data(MP4File & mp4, const uint64_t data_size);
  void print_structure(const unsigned int indent = 0);

private:
  uint8_t version_;
  uint32_t flags_;
  uint32_t track_ID_;
  uint64_t base_data_offset_;
  uint32_t sample_description_index_;
  uint32_t default_sample_duration_;
  uint32_t default_sample_size_;
  uint32_t default_sample_flags_;
};

}
#endif /* TFHD_BOX_HH */
