#ifndef MP4_PARSER_HH
#define MP4_PARSER_HH

#include <memory>
#include <vector>
#include <string>
#include <cstdint>

#include "mp4_box.hh"
#include "mp4_file.hh"

namespace MP4 {

class Parser
{
public:
  Parser(const std::string & filename);

  void parse();
  void print_structure();
  void split(const std::string & init_seg,
             const std::string & media_seg_template,
             const unsigned int start_number);

private:
  MP4File file_;
  std::unique_ptr<Box> box_;

  void create_boxes(std::unique_ptr<Box> & parent_box,
                    const int64_t start_offset, const int64_t total_size);
  void copy_to_file(FileDescriptor & output_fd, const int64_t copy_size);
};

}

#endif /* MP4_PARSER_HH */
