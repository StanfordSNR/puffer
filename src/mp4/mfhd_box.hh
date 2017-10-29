#ifndef MFHD_BOX_HH
#define MFHD_BOX_HH

#include "box.hh"

namespace MP4 {

class MfhdBox : public FullBox
{
public:
  MfhdBox(const uint64_t size, const std::string & type);

  /* accessors */
  uint32_t sequence_number() { return sequence_number_; }

  void print_structure(const unsigned int indent = 0);

  void parse_data(MP4File & mp4, const uint64_t data_size);

private:
  uint32_t sequence_number_;
};

}

#endif /* MFHD_BOX_HH */
