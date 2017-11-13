#ifndef STSS_BOX_HH
#define STSS_BOX_HH

#include <string>
#include <vector>

#include "box.hh"

namespace MP4 {

class StssBox : public FullBox
{
public:
  StssBox(const uint64_t size, const std::string & type);

  /* accessors */
  uint32_t entry_count() { return entries_.size(); }
  std::vector<uint32_t> entries() { return entries_; }

  /* mutators */
  void set_entries(std::vector<uint32_t> entries);

  void print_box(const unsigned int indent = 0);

  void parse_data(MP4File & mp4, const uint64_t data_size);
  void write_box(MP4File & mp4);

private:
  std::vector<uint32_t> entries_;
};

} /* namespace MP4 */

#endif /* STSS_BOX_HH */
