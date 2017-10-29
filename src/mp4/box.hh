#ifndef BOX_HH
#define BOX_HH

#include <string>
#include <vector>
#include <memory>
#include <cstdint>

#include "mp4_file.hh"

namespace MP4 {

class Box
{
public:
  Box(const uint64_t size, const std::string & type);
  virtual ~Box() {}

  /* accessors */
  uint64_t size() { return size_; }
  std::string type() { return type_; }

  /* parameter is a sink; use rvalue reference to save a "move" operation */
  void add_child(std::shared_ptr<Box> && child);

  std::vector<std::shared_ptr<Box>>::const_iterator children_begin();
  std::vector<std::shared_ptr<Box>>::const_iterator children_end();

  /* find the first box of 'type' in descendants (excluding itself) */
  std::shared_ptr<Box> find_first_descendant_of(const std::string & type);

  /* print box structure */
  virtual void print_structure(const unsigned int indent = 0);

  /* parse the next 'data_size' bytes in 'mp4' */
  virtual void parse_data(MP4File & mp4, const uint64_t data_size);

protected:
  /* a helper function typically used in 'parse_data'; skip parsing the next
   * 'data_size - (mp4.curr_offset() - init_offset)' bytes of data in 'mp4' */
  void skip_data(MP4File & mp4, const uint64_t data_size,
                 const uint64_t init_offset);

private:
  uint64_t size_;
  std::string type_;

  std::vector<std::shared_ptr<Box>> children_;
};

class FullBox : public Box
{
public:
  FullBox(const uint64_t size, const std::string & type);

  void parse_data(MP4File & mp4);

  /* accessors */
  uint8_t version() { return version_; }
  uint32_t flags() { return flags_; }

private:
  uint8_t version_;
  uint32_t flags_;
};

}

#endif /* BOX_HH */
