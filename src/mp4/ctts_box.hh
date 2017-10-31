#ifndef CTTS_BOX_HH
#define CTTS_BOX_HH

#include "box.hh"

namespace MP4 {

class CttsBox : public FullBox
{
public:
  CttsBox(const uint64_t size, const std::string & type);

  /* accessors */
  std::vector<int64_t> get_sample_offsets() { return offset_entries_; }

  void print_structure(const unsigned int indent = 0);

  void parse_data(MP4File & mp4, const uint64_t data_size);

private:
  /* use int64_t to hold both unsigned and signed int32 */
  std::vector<int64_t> offset_entries_;
};

}
#endif /* Ctts_BOX_HH */
