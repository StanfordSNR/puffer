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
  Box(const uint32_t size, const std::string & type);

  std::string get_type();
  void add_child(std::unique_ptr<Box> child);

  std::vector<std::unique_ptr<Box>>::iterator children_begin();
  std::vector<std::unique_ptr<Box>>::iterator children_end();

private:
  uint32_t size_;
  std::string type_;
  std::vector<std::unique_ptr<Box>> children_;
};

}

#endif /* MP4_BOX_HH */
