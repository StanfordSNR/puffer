#ifndef MP4_HH
#define MP4_HH

#include <memory>
#include <vector>
#include <string>
#include <cstdint>
#include <tuple>

#include "file_descriptor.hh"

namespace MP4 {

class MP4File : public FileDescriptor
{
public:
  MP4File(const std::string & filename);

  /* manipulate file offset */
  int64_t seek(const int64_t offset, const int whence);
  int64_t curr_offset();
  int64_t inc_offset(const int64_t offset);

  int64_t filesize();

  /* reset file offset to the beginning and set EOF to false */
  void reset();

  /* read bytes from file and return meaningful data */
  uint16_t read_uint16();
  uint32_t read_uint32();
  uint64_t read_uint64();
  std::tuple<uint8_t, uint32_t> read_version_flags();
};

class Box
{
public:
  Box(const uint64_t size, const std::string & type);
  virtual ~Box() {}

  /* accessors */
  uint64_t size() { return size_; }
  std::string type() { return type_; }

  /* parameter is a sink; use rvalue reference to save a "move" operation */
  void add_child(std::unique_ptr<Box> && child);

  std::vector<std::unique_ptr<Box>>::const_iterator children_begin();
  std::vector<std::unique_ptr<Box>>::const_iterator children_end();

  virtual void print_structure(int indent = 0);

  virtual void parse_data(MP4File & mp4, const int64_t DATa_size);

private:
  uint64_t size_;
  std::string type_;

  std::vector<std::unique_ptr<Box>> children_;
};

/* ISO/IEC 14496-12:2015 Section 8.2.2 */
class MvhdBox : public Box
{
public:
  MvhdBox(const uint64_t size, const std::string & type);
  ~MvhdBox() {}

  /* accessors */
  uint8_t version() { return version_; }
  uint32_t flags() { return flags_; }
  uint64_t creation_time() { return creation_time_; }
  uint64_t modificaiton_time() { return modification_time_; }
  uint32_t timescale() { return timescale_; }
  uint64_t duration() { return duration_; }

  void print_structure(int indent = 0);

  void parse_data(MP4File & mp4, const int64_t data_size);

private:
  uint8_t version_;
  uint32_t flags_;
  uint64_t creation_time_;
  uint64_t modification_time_;
  uint32_t timescale_;
  uint64_t duration_;
};

/* ISO/IEC 14496-12:2015 Section 8.16.3 */
class SidxBox: public Box
{
public:
  struct SidxReference {
    bool        reference_type;
    uint32_t    reference_size;
    uint32_t    segment_duration;
    bool        starts_with_SAP;
    uint8_t     SAP_type;
    uint32_t    SAP_delta;
  };

  SidxBox(const uint64_t size, const std::string & type);
  ~SidxBox() {}

  /* accessors */
  uint8_t version() { return version_; }
  uint32_t flags() { return flags_; }
  uint32_t reference_id() { return reference_id_; }
  uint32_t timescale() { return timescale_; }
  uint64_t earlist_presentation_time() { return earlist_presentation_time_; }
  uint64_t first_offset() { return first_offset_; }
  uint16_t reserved() { return reserved_; }

  /* take rvalue and move it, rather than copy */
  void add_reference(SidxReference && ref);

  void print_structure(int indent = 0);

  void parse_data(MP4File & mp4, const int64_t data_size);

private:
  uint8_t version_;
  uint32_t flags_;
  uint32_t reference_id_;
  uint32_t timescale_;
  uint64_t earlist_presentation_time_;
  uint64_t first_offset_;
  uint16_t reserved_;

  std::vector<SidxReference> reference_list_;
};

class Parser
{
public:
  Parser(const std::string & filename);

  /* parse MP4 into boxes */
  void parse();

  /* print MP4 structure, i.e., type and size of each box, and box hierarchy
   * call after parse() */
  void print_structure();

  /* split fragmented MP4 into initial segment and media segments
   * call after parse() */
  void split(const std::string & init_seg,
             const std::string & media_seg_template,
             const unsigned int start_number);

private:
  MP4File file_;
  std::unique_ptr<Box> box_;

  /* recursively create boxes between start_offset and its following total_size
   * add created boxes as children of parent_box */
  void create_boxes(const std::unique_ptr<Box> & parent_box,
                    const int64_t start_offset, const int64_t total_size);

  /* copy size_to_copy bytes from current offset and write to filename */
  void copy_to_file(const std::string & output_filename,
                    const int64_t size_to_copy);

  std::string populate_template(const std::string & media_seg_template,
                                const unsigned int curr_seg_number);
};

}

#endif /* MP4_HH */
