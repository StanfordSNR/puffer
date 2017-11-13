#ifndef SIDX_BOX_HH
#define SIDX_BOX_HH

#include <string>
#include <vector>

#include "box.hh"

namespace MP4 {

class SidxBox : public FullBox
{
public:
  struct SidxReference {
    bool     reference_type;
    uint32_t referenced_size;
    uint32_t subsegment_duration;
    bool     starts_with_sap;
    uint8_t  sap_type;
    uint32_t sap_delta;
  };

  SidxBox(const uint64_t size, const std::string & type);
  SidxBox(const std::string & type,
          const uint8_t version,
          const uint32_t flags,
          const uint32_t reference_id,
          const uint32_t timescale,
          const uint64_t earlist_presentation_time,
          const uint64_t first_offset,
          const std::vector<SidxReference> & reference_list);

  /* accessors */
  uint32_t reference_id() { return reference_id_; }
  uint32_t timescale() { return timescale_; }
  uint64_t earlist_presentation_time() { return earlist_presentation_time_; }
  uint64_t first_offset() { return first_offset_; }
  uint16_t reference_count() { return reference_list_.size(); }

  /* sum of the subsegment_duration */
  uint32_t duration();

  unsigned int reference_list_pos();

  void print_box(const unsigned int indent = 0);

  void parse_data(MP4File & mp4, const uint64_t data_size);
  void write_box(MP4File & mp4);

private:
  uint32_t reference_id_;
  uint32_t timescale_;
  uint64_t earlist_presentation_time_;
  uint64_t first_offset_;

  std::vector<SidxReference> reference_list_;
};

} /* namespace MP4 */

#endif /* SIDX_BOX_HH */
