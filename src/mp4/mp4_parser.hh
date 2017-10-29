#ifndef MP4_PARSER_HH
#define MP4_PARSER_HH

#include <string>
#include <memory>
#include <cstdint>

#include "mp4_file.hh"
#include "box.hh"

namespace MP4 {

class MP4Parser
{
public:
  MP4Parser(const std::string & filename);

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
  MP4File mp4_;
  std::shared_ptr<Box> box_;

  /* recursively create boxes between start_offset and its following total_size
   * add created boxes as children of parent_box */
  void create_boxes(const std::shared_ptr<Box> & parent_box,
                    const uint64_t start_offset, const uint64_t total_size);

  /* a factory method to create different boxes based on their type */
  std::shared_ptr<Box> box_factory(const uint64_t size,
    const std::string & type, MP4File & mp4, const uint64_t data_size);

  /* copy size_to_copy bytes from current offset and write to filename */
  void copy_to_file(const std::string & output_filename,
                    const uint64_t size_to_copy);

  std::string populate_template(const std::string & media_seg_template,
                                const unsigned int curr_seg_number);
};

}

#endif /* MP4_PARSER_HH */
