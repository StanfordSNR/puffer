#ifndef MFHD_BOX_HH
#define MFHD_BOX_HH

#include <string>

#include "box.hh"

namespace MP4 {

class MfhdBox : public FullBox
{
public:
  MfhdBox(const uint64_t size, const std::string & type);
  MfhdBox(const std::string & type,
          const uint8_t version,
          const uint32_t flags,
          const uint32_t sequence_number);

  /* accessors */
  uint32_t sequence_number() { return sequence_number_; }

  void print_box(const unsigned int indent = 0);

  void parse_data(MP4File & mp4, const uint64_t data_size);
  void write_box(MP4File & mp4);

private:
  uint32_t sequence_number_;
};

} /* namespace MP4 */

#endif /* MFHD_BOX_HH */
