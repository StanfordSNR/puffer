#ifndef TRUN_BOX_HH
#define TRUN_BOX_HH

#include "box.hh"

namespace MP4 {

class TrunBox : public Box
{
public:
  TrunBox(const uint64_t size, const std::string & type);

  /* accessors */
  uint8_t version() { return version_; }
  uint32_t flags() { return flags_; }
  uint32_t sample_count () { return sample_count_; }
  uint32_t duration() { return duration_; }

  void parse_data(MP4File & mp4, const uint64_t data_size);
  void print_structure(const unsigned int indent = 0);

private:
  uint8_t version_;
  uint32_t flags_;
  uint32_t sample_count_;
  uint32_t duration_;
};

}
#endif /* TRUN_BOX_HH */
