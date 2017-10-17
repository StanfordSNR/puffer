#ifndef MP4_BOX_HH
#define MP4_BOX_HH

#include <memory>
#include <vector>
#include <string>
#include <cstdint>

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

}

#endif /* MP4_BOX_HH */
