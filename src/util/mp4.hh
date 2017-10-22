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

  void print_structure(int indent = 0);

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
  uint64_t read_uint64();

  void reset();
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
