#ifndef MP4_PARSER_HH
#define MP4_PARSER_HH

#include <memory>
#include <string>
#include <cstdint>

#include "mp4_box.hh"
#include "mp4_file.hh"

namespace MP4 {

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

#endif /* MP4_PARSER_HH */
