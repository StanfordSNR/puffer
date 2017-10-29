#ifndef TFDT_BOX_HH
#define TFDT_BOX_HH

#include "box.hh"

namespace MP4 {

class TfdtBox : public FullBox
{
public:
  TfdtBox(const uint64_t size, const std::string & type);

  /* accessors */
  uint64_t base_media_decode_time() { return base_media_decode_time_; }

  void print_structure(const unsigned int indent = 0);

  void parse_data(MP4File & mp4, const uint64_t data_size);

private:
  uint64_t base_media_decode_time_;
};

}

#endif /* TFDT_BOX_HH */
