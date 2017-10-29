#ifndef MVHD_BOX_HH
#define MVHD_BOX_HH

#include "box.hh"

namespace MP4 {

class MvhdBox : public Box
{
public:
  MvhdBox(const uint64_t size, const std::string & type);

  /* accessors */
  uint8_t version() { return version_; }
  uint32_t flags() { return flags_; }
  uint64_t creation_time() { return creation_time_; }
  uint64_t modificaiton_time() { return modification_time_; }
  uint32_t timescale() { return timescale_; }
  uint64_t duration() { return duration_; }

  void print_structure(const unsigned int indent = 0);

  void parse_data(MP4File & mp4, const uint64_t data_size);

private:
  uint8_t version_;
  uint32_t flags_;
  uint64_t creation_time_;
  uint64_t modification_time_;
  uint32_t timescale_;
  uint64_t duration_;
};

}

#endif /* MVHD_BOX_HH */
