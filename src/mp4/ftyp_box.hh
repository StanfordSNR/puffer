#ifndef FTYP_BOX_HH
#define FTYP_BOX_HH

#include <string>
#include <vector>

#include "box.hh"

namespace MP4 {

class FtypBox : public Box
{
public:
  FtypBox(const uint64_t size, const std::string & type);
  FtypBox(const std::string & type,
          const std::string & major_brand,
          const uint32_t minor_version,
          const std::vector<std::string> & compatible_brands);

  /* accessors */
  std::string major_brand() { return major_brand_; }
  uint32_t minor_version() { return minor_version_; }
  std::vector<std::string> compatible_brands() { return compatible_brands_; }

  /* add brand if it does not exist in compatible_brands_ */
  void add_compatible_brand(std::string brand);

  void print_box(const unsigned int indent = 0);

  void parse_data(MP4File & mp4, const uint64_t data_size);
  void write_box(MP4File & mp4);

private:
  std::string major_brand_;
  uint32_t minor_version_;
  std::vector<std::string> compatible_brands_;
};

} /* namespace MP4 */

#endif /* FTYP_BOX_HH */
