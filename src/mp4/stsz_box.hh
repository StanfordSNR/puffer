#ifndef STSZ_BOX_HH
#define STSZ_BOX_HH

#include <string>
#include <vector>

#include "box.hh"

namespace MP4 {

class StszBox : public FullBox
{
public:
  StszBox(const uint64_t size, const std::string & type);
  StszBox(const std::string & type,
          const uint8_t version,
          const uint32_t flags,
          const uint32_t sample_size,
          std::vector<uint32_t> entries);

  /* accessors */
  uint32_t sample_size() { return sample_size_; }
  uint32_t sample_count() { return entries_.size(); }
  std::vector<uint32_t> entries() { return entries_; }

  /* mutators */
  void set_sample_size(const uint32_t sample_size);
  void set_entries(std::vector<uint32_t> entries);

  void print_box(const unsigned int indent = 0);

  void parse_data(MP4File & mp4, const uint64_t data_size);
  void write_box(MP4File & mp4);

private:
  uint32_t sample_size_;

  std::vector<uint32_t> entries_;
};

} /* namespace MP4 */

#endif /* STSZ_BOX_HH */
