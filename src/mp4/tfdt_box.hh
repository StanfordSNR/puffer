#ifndef TFDT_BOX_HH
#define TFDT_BOX_HH

#include <string>

#include "box.hh"

namespace MP4 {

class TfdtBox : public FullBox
{
public:
  TfdtBox(const uint64_t size, const std::string & type);
  TfdtBox(const std::string & type,
          const uint8_t version,
          const uint32_t flags,
          const uint64_t base_media_decode_time);

  /* accessors */
  uint64_t base_media_decode_time() { return base_media_decode_time_; }

  void print_box(const unsigned int indent = 0);

  void parse_data(MP4File & mp4, const uint64_t data_size);
  void write_box(MP4File & mp4);

private:
  uint64_t base_media_decode_time_;
};

} /* namespace MP4 */

#endif /* TFDT_BOX_HH */
