#include <iostream>

#include "ftyp_box.hh"

using namespace std;
using namespace MP4;

FtypBox::FtypBox(const uint64_t size, const string & type)
  : Box(size, type), major_brand_(), minor_version_(), compatible_brands_()
{}

FtypBox::FtypBox(const string & type,
                 const string & major_brand,
                 const uint32_t minor_version,
                 const vector<string> & compatible_brands)
  : Box(type), major_brand_(major_brand), minor_version_(minor_version),
    compatible_brands_(compatible_brands)
{}

void FtypBox::add_compatible_brand(string brand)
{
  for (const auto & existing_brand : compatible_brands_) {
    if (existing_brand == brand) {
      return;
    }
  }

  compatible_brands_.emplace_back(move(brand));
}

void FtypBox::print_box(const unsigned int indent)
{
  print_size_type(indent);

  string indent_str = string(indent + 2, ' ') + "| ";
  cout << indent_str << "major brand " << major_brand_ << endl;
  cout << indent_str << "minor version 0x"
       << hex << minor_version_ << dec << endl;

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

void FtypBox::write_box(MP4File & mp4)
{
  uint64_t size_offset = mp4.curr_offset();

  write_size_type(mp4);

  mp4.write_string(major_brand_, 4);
  mp4.write_uint32(minor_version_);

  for (const auto & brand : compatible_brands_) {
    mp4.write_string(brand, 4);
  }

  fix_size_at(mp4, size_offset);
}
