#ifndef MDHD_BOX_HH
#define MDHD_BOX_HH

#include <string>

#include "box.hh"

namespace MP4 {

class MdhdBox : public FullBox
{
public:
  MdhdBox(const uint64_t size, const std::string & type);
  MdhdBox(const std::string & type,
          const uint8_t version,
          const uint32_t flags,
          const uint64_t creation_time,
          const uint64_t modification_time,
          const uint32_t timescale,
          const uint64_t duration,
          const uint16_t language);

  /* accessors */
  uint64_t creation_time() { return creation_time_; }
  uint64_t modificaiton_time() { return modification_time_; }
  uint32_t timescale() { return timescale_; }
  uint64_t duration() { return duration_; }

  /* mutators */
  void set_timescale(const uint32_t timescale) { timescale_ = timescale; }
  void set_duration(const uint64_t duration) { duration_ = duration; }

  void print_box(const unsigned int indent = 0);

  void parse_data(MP4File & mp4, const uint64_t data_size);
  void write_box(MP4File & mp4);

private:
  uint64_t creation_time_;
  uint64_t modification_time_;
  uint32_t timescale_;
  uint64_t duration_;
  uint16_t language_;
};

} /* namespace MP4 */

#endif /* MDHD_BOX_HH */
