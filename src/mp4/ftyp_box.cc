#include <iostream>

#include "ftyp_box.hh"

using namespace std;
using namespace MP4;

FtypBox::FtypBox(const uint64_t size, const string & type)
  : Box(size, type), major_brand_(), minor_version_(), compatible_brands_()
{}

void FtypBox::print_structure(const unsigned int indent)
{
  print_size_type(indent);

  string indent_str = string(indent + 2, ' ') + "| ";
  cout << indent_str << "major brand " << major_brand_ << endl;
  cout << indent_str << "minor version " << minor_version_ << endl;

  cout << indent_str << "compatible brands";
  for (const auto & brand : compatible_brands_) {
    cout << " " << brand;
  }
  cout << endl;
}

void FtypBox::parse_data(MP4File & mp4, const uint64_t data_size)
{
  uint64_t init_offset = mp4.curr_offset();

  major_brand_ = mp4.read(4);
  minor_version_ = mp4.read_uint32();

  uint64_t data_parsed = 8;
  while (data_parsed < data_size) {
    compatible_brands_.emplace_back(mp4.read(4));
    data_parsed += 4;
  }

  check_data_left(mp4, data_size, init_offset);
}
