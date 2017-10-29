#ifndef TFHD_BOX_HH
#define TFHD_BOX_HH

#include "box.hh"

namespace MP4 {

class TfhdBox : public FullBox
{
public:
  TfhdBox(const uint64_t size, const std::string & type);

  /* accessors */
  uint32_t track_id() { return track_id_; }

  void print_structure(const unsigned int indent = 0);

  void parse_data(MP4File & mp4, const uint64_t data_size);

private:
  uint32_t track_id_;
};

}

#endif /* TFHD_BOX_HH */
