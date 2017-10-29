#ifndef SIDX_BOX_HH
#define SIDX_BOX_HH

#include "box.hh"

namespace MP4 {

class SidxBox : public FullBox
{
public:
  struct SidxReference {
    bool     reference_type;
    uint32_t reference_size;
    uint32_t subsegment_duration;
    bool     starts_with_sap;
    uint8_t  sap_type;
    uint32_t sap_delta;
  };

  SidxBox(const uint64_t size, const std::string & type);

  /* accessors */
  uint32_t reference_id() { return reference_id_; }
  uint32_t timescale() { return timescale_; }
  uint64_t earlist_presentation_time() { return earlist_presentation_time_; }
  uint64_t first_offset() { return first_offset_; }
  uint16_t reserved() { return reserved_; }

  /* take rvalue and move it, rather than copy */
  void add_reference(SidxReference && ref);

  void print_structure(const unsigned int indent = 0);

  void parse_data(MP4File & mp4, const uint64_t data_size);
  /* sum of the subsegment_duration */
  uint32_t duration();
private:
  uint32_t reference_id_;
  uint32_t timescale_;
  uint64_t earlist_presentation_time_;
  uint64_t first_offset_;
  uint16_t reserved_;

  std::vector<SidxReference> reference_list_;
};

}

#endif /* SIDX_BOX_HH */
