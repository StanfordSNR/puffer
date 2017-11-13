#ifndef STTS_BOX_HH
#define STTS_BOX_HH

#include <string>
#include <vector>

#include "box.hh"

namespace MP4 {

class SttsBox : public FullBox
{
public:
  struct Entry {
    uint32_t sample_count;
    uint32_t sample_delta;
  };

  SttsBox(const uint64_t size, const std::string & type);

  /* accessors */
  uint32_t entry_count() { return entries_.size(); }
  std::vector<Entry> entries() { return entries_; }

  uint32_t total_sample_count();

  /* mutators */
  void set_entries(std::vector<Entry> entries);

  void print_box(const unsigned int indent = 0);

  void parse_data(MP4File & mp4, const uint64_t data_size);
  void write_box(MP4File & mp4);

private:
  std::vector<Entry> entries_;
};

} /* namespace MP4 */

#endif /* STTS_BOX_HH */
