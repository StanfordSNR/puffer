#include <string>
#include <algorithm>
#include "exception.hh"
#include "webm_parser.hh"

using namespace std;
using namespace webm;

template<typename T> T BinaryReader::read()
{
  string data = fd_.read(sizeof(T));
  const T * size = reinterpret_cast<const T *>(data.c_str());
  return *size;
}

uint64_t BinaryReader::size()
{
  uint64_t current_pos = pos();
  uint64_t fsize = seek(0, SEEK_END);
  seek(current_pos, SEEK_SET);
  return fsize;
}

WebmParser::WebmParser(const string & filename)
  : br_(filename)
{
  parse(br_.size());
}

void WebmParser::parse(uint64_t max_pos)
{
  while (br_.pos() < max_pos ) {
    const uint32_t tag = scan_tag();
    uint64_t data_size = scan_data_size();
    cout << "TAG: 0x" << hex << tag << " Size: " << data_size << endl;
    if (find(begin(master_elements), end(master_elements), tag)
        == end(master_elements)) {
      br_.read_bytes(data_size);
    } else {
      parse(br_.pos() + data_size);
    }
  }
}

uint64_t WebmParser::scan_tag()
{
  const uint8_t first_byte = br_.read_uint8();
  const uint8_t mask = 0xFF;
  uint32_t tag_size;
  if (first_byte & 0x80) {
    tag_size = 0;
  } else if (first_byte & 0x40) {
    tag_size = 1;
  } else if (first_byte & 0x20) {
    tag_size = 2;
  } else if (first_byte & 0x10) {
    tag_size = 3;
  } else {
    cout << unsigned(first_byte) << endl;
    throw runtime_error("Invalid tag type at pos " + to_string(br_.pos()));
  }
  return decode_bytes(tag_size, first_byte, mask);
}

uint64_t WebmParser::decode_bytes(uint32_t tag_size, uint8_t first,
                                uint8_t first_mask)
{
  uint64_t value = first & first_mask;
  for (uint32_t i = 0; i < tag_size; i++) {
    uint8_t next_byte = br_.read_uint8();
    value = (value << 8) + next_byte;
  }
  return value;
}

uint64_t WebmParser::scan_data_size()
{
  uint32_t size = 0;
  uint32_t mask = 0xFF;

  uint8_t first_byte = br_.read_uint8();
  for (int i = 0; i < 8; i++) {
    if (first_byte & (0x80 >> i)) {
      size = i;
      mask = 0x7F >> i;
      break;
    }
  }
  return decode_bytes(size, first_byte, mask);
}
