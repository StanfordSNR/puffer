#ifndef TKHD_BOX_HH
#define TKHD_BOX_HH

#include "box.hh"

namespace MP4 {

class TkhdBox : public FullBox
{
public:
  TkhdBox(const uint64_t size, const std::string & type);

  /* accessors */
  uint64_t creation_time() { return creation_time_; }
  uint64_t modification_time() { return modification_time_; }
  uint32_t track_id() { return track_id_; }
  uint64_t duration() { return duration_; }
  uint32_t width() { return width_; }
  uint32_t height() { return height_; }

  void print_structure(const unsigned int indent = 0);

  void parse_data(MP4File & mp4, const uint64_t data_size);

private:
  uint64_t creation_time_;
  uint64_t modification_time_;
  uint32_t track_id_;
  uint64_t duration_;
  uint32_t width_;
  uint32_t height_;
};

}
#endif /* TKHD_BOX_HH */
