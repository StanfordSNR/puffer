#ifndef TREX_BOX_HH
#define TREX_BOX_HH

#include "box.hh"

namespace MP4 {

class TrexBox : public FullBox
{
public:
  TrexBox(const uint64_t size, const std::string & type);

  /* accessors */
  uint32_t track_id() { return track_id_; }
  uint32_t default_sample_description_index()
    { return default_sample_description_index_; }
  uint32_t default_sample_duration() { return default_sample_duration_; }
  uint32_t default_sample_size() { return default_sample_size_; }
  uint32_t default_sample_flags() { return default_sample_flags_; }

  void print_structure(const unsigned int indent = 0);

  void parse_data(MP4File & mp4, const uint64_t data_size);

private:
  uint32_t track_id_;
  uint32_t default_sample_description_index_;
  uint32_t default_sample_duration_;
  uint32_t default_sample_size_;
  uint32_t default_sample_flags_;
};

}

#endif /* TREX_BOX_HH */
