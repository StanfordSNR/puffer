#ifndef ELST_BOX_HH
#define ELST_BOX_HH

#include <string>
#include <vector>

#include "box.hh"

namespace MP4 {

class ElstBox : public FullBox
{
public:
  struct Edit {
    uint64_t segment_duration;
    int64_t media_time;
    int16_t media_rate_integer;
    int16_t media_rate_fraction;
  };

  ElstBox(const uint64_t size, const std::string & type);
  ElstBox(const std::string & type,
          const uint8_t version,
          const uint32_t flags,
          const std::vector<Edit> & edit_list);

  /* accessors */
  std::vector<Edit> edit_list() { return edit_list_; }

  void set_segment_duration(const uint64_t duration);

  void print_box(const unsigned int indent = 0);

  void parse_data(MP4File & mp4, const uint64_t data_size);
  void write_box(MP4File & mp4);

private:
  std::vector<Edit> edit_list_;
};

} /* namespace MP4 */

#endif /* ELST_BOX_HH */
