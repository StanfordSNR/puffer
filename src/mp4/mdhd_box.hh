#ifndef MDHD_BOX_HH
#define MDHD_BOX_HH

#include "box.hh"

namespace MP4 {

class MdhdBox : public FullBox
{
public:
  MdhdBox(const uint64_t size, const std::string & type);

  /* accessors */
  uint64_t creation_time() { return creation_time_; }
  uint64_t modificaiton_time() { return modification_time_; }
  uint32_t timescale() { return timescale_; }
  uint64_t duration() { return duration_; }
  uint16_t language() { return language_; }

  void print_structure(const unsigned int indent = 0);

  void parse_data(MP4File & mp4, const uint64_t data_size);

private:
  uint64_t creation_time_;
  uint64_t modification_time_;
  uint32_t timescale_;
  uint64_t duration_;
  uint16_t language_;
};

}

#endif /* MDHD_BOX_HH */
