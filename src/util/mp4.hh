#ifndef MP4_HH
#define MP4_HH
#include <memory>
#include <vector>
#include <string>
#include <cstdint>
#include "file_descriptor.hh"

namespace MP4 {

class Box
{
public:
  Box(const uint64_t size, const std::string & type);
  /* accessors */
  uint64_t size();
  std::string type();

  void add_child(std::unique_ptr<Box> child);
  std::vector<std::unique_ptr<Box>>::iterator children_begin();
  std::vector<std::unique_ptr<Box>>::iterator children_end();

  virtual void print_structure(int indent = 0);

  virtual ~Box() {}
private:
  uint64_t size_;
  std::string type_;
  std::vector<std::unique_ptr<Box>> children_;

};

class MP4File : public FileDescriptor
{
public:
  MP4File(const std::string & filename);

  inline int64_t seek(int64_t offset, int whence);
  int64_t curr_offset();
  int64_t inc_offset(int64_t offset);
  int64_t filesize();

  uint32_t read_uint32();
  uint16_t read_uint16();
  uint64_t read_uint64();

  void reset();
};

/* ISO/IEC 14496-12:2015 Section 8.2.2 */
class MvhdBox : public Box
{
public:
  MvhdBox(const uint64_t size, const std::string & type) :
    Box(size, type), version_(), flags_(), creation_time_(), 
    modification_time_(), timescale_(), duration_()
  {}

  MvhdBox(const uint64_t size, const std::string & type, MP4File & mp4);

  uint8_t version() { return this->version_; }
  uint32_t flags() { return this->flags_; }
  uint64_t creation_time() { return this->creation_time_; }
  uint64_t modificaiton_time() { return this->modification_time_; }
  uint32_t timescale() { return this->timescale_; }
  uint64_t duration() { return this->duration_; }

  void print_structure(int indent = 0);
  ~MvhdBox() {}
private:
  uint8_t   version_;
  uint32_t  flags_;
  uint64_t  creation_time_;
  uint64_t  modification_time_;
  uint32_t  timescale_;
  uint64_t  duration_;
};

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

  SidxBox(const uint64_t size, const std::string & type)
    : Box(size, type), version_(), flags_(), reference_ID_(),
    timescale_(), earlist_presentation_time_(), first_offset_(), 
    reserved_(), reference_list_()
  {}

  SidxBox(const uint64_t size, const std::string & type, MP4File & mp4);


  void print_structure(int indent = 0);
  ~SidxBox() {}

  uint8_t   version() { return version_; }
  uint32_t  flags() { return flags_; }
  uint32_t  reference_ID() { return reference_ID_; }
  uint32_t  timescale() { return timescale_; }
  uint64_t  earlist_presentation_time() {
    return this->earlist_presentation_time_; }
  uint64_t  first_offset() { return first_offset_; }
  void add_reference(struct SidxReference & ref) {
    this->reference_list_.push_back(ref);
  }

private:
  uint8_t   version_;
  uint32_t  flags_;
  uint32_t  reference_ID_;
  uint32_t  timescale_;
  uint64_t  earlist_presentation_time_;
  uint64_t  first_offset_;
  uint16_t  reserved_;
  std::vector<struct SidxReference> reference_list_;
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
  void create_boxes(std::unique_ptr<Box> & parent_box,
                    const int64_t start_offset, const int64_t total_size);

  /* copy size_to_copy bytes from current offset and write to filename */
  void copy_to_file(const std::string & output_filename,
                    const int64_t size_to_copy);

  std::string populate_template(const std::string & media_seg_template,
                                const unsigned int curr_seg_number);
};

}



#endif
