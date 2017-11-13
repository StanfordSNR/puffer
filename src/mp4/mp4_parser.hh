#ifndef MP4_PARSER_HH
#define MP4_PARSER_HH

#include <cstdint>
#include <string>
#include <memory>
#include <set>

#include "mp4_file.hh"
#include "box.hh"

namespace MP4 {

const std::set<std::string> mp4_container_boxes{
  "moov", "trak", "edts", "mdia", "minf", "stbl", "mvex", "moof", "traf",
  "mfra", "skip", "strk", "meta", "dinf", "ipro", "sinf", "fiin", "paen",
  "meco", "mere"};

class MP4Parser
{
public:
  MP4Parser();
  MP4Parser(const std::string & mp4_file);

  /* parse MP4 into boxes */
  void parse();

  /* skip parsing 'type' box but save it in raw data */
  void ignore_box(const std::string & type);
  bool is_ignored(const std::string & type);

  std::shared_ptr<Box> find_first_box_of(const std::string & type);

  bool is_video();
  bool is_audio();

  /* print the type and size of each box, and the box structure of MP4 */
  void print_structure();

  void add_top_level_box(std::shared_ptr<Box> && top_level_box);

  void save_to_mp4(MP4File & mp4);

protected:
  /* accessors */
  std::shared_ptr<MP4File> mp4() { return mp4_; }
  std::shared_ptr<Box> root_box() { return root_box_; }

private:
  std::shared_ptr<MP4File> mp4_;
  std::shared_ptr<Box> root_box_;

  std::set<std::string> ignored_boxes_;

  /* a factory method to create different boxes based on their type */
  std::shared_ptr<Box> box_factory(const uint64_t size,
                                   const std::string & type,
                                   const uint64_t data_size);

  /* recursively create boxes between 'start_offset' and its following
   * 'total_size' bytes; add created boxes as children of the 'parent_box' */
  void create_boxes(const std::shared_ptr<Box> & parent_box,
                    const uint64_t start_offset,
                    const uint64_t total_size);

  std::shared_ptr<Box> do_find_first_box_of(const std::shared_ptr<Box> & box,
                                            const std::string & type);
};

} /* namespace MP4 */

#endif /* MP4_PARSER_HH */
