#include <string>
#include <algorithm>
#include "exception.hh"
#include "webm_info.hh"

using namespace std;

const uint32_t default_timecode_scale = 1000000;

template<typename T> T BinaryReader::read(bool switch_endian)
{
  string data = fd_.read(sizeof(T));
  return read_raw<T>(data, sizeof(T), switch_endian);
}

uint64_t BinaryReader::size()
{
  uint64_t current_pos = pos();
  uint64_t fsize = seek(0, SEEK_END);
  seek(current_pos, SEEK_SET);
  return fsize;
}

shared_ptr<WebmElement> WebmElement::find_first(const uint32_t tag)
{
  if (tag_ == tag) {
    return shared_from_this();
  } else {
    for (const auto elem : children_) {
      auto result = elem->find_first(tag);
      if (result) {
        return result;
      }
    }
  }
  return nullptr;
}

set<shared_ptr<WebmElement>> WebmElement::find_all(const uint32_t tag)
{
  set<shared_ptr<WebmElement>> result;
  if (tag_ == tag) {
    result.insert(shared_from_this());
  } else {
    for (const auto elem : children_) {
      for (const auto r : elem->find_all(tag)) {
        result.insert(r);
      }
    }
  }
  return result;
}

WebmParser::WebmParser(const string & filename)
  : br_(filename)
{
  parse(br_.size(), root_);
}

void WebmParser::parse(uint64_t max_pos, shared_ptr<WebmElement> parent)
{
  while (br_.pos() < max_pos) {
    const uint32_t tag = scan_tag();
    if (tag == 0) {
      return; /* terminate */
    }
    uint64_t data_size = scan_data_size();
    if (data_size == static_cast<uint64_t>(-1)) {
      auto elem = make_shared<WebmElement>(tag, "");
      parent->add_element(elem);
      continue;
    }
    if (master_elements.find(tag) == master_elements.end()) {
      auto elem = make_shared<WebmElement>(tag, br_.read_bytes(data_size));
      parent->add_element(elem);
    } else {
      /* master block */
      auto elem = make_shared<WebmElement>(tag, data_size);
      parent->add_element(elem);
      parse(br_.pos() + data_size, elem);
    }
  }
}

uint64_t WebmParser::scan_tag()
{
  const uint8_t first_byte = br_.read_uint8();
  const uint8_t mask = 0xFF;
  uint32_t tag_size = 4; /* by default it's an impossible state */
  if (!first_byte) {
    return 0; /* end of stream */
  }
  for (uint32_t i = 0; i < 4; i++) {
    if (first_byte & (0x80 >> i)) {
      tag_size = i;
      break;
    }
  }
  if (tag_size == 4) {
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

shared_ptr<WebmElement> WebmParser::find_first(const uint32_t tag)
{
  return root_->find_first(tag);
}

set<shared_ptr<WebmElement>> WebmParser::find_all(const uint32_t tag)
{
  return root_->find_all(tag);
}

uint32_t WebmInfo::get_timescale()
{
  auto elm = parser_.find_first(ElementTagID::TimecodeScale);
  if (elm) {
    uint32_t data_size = elm->size();
    string data = elm->value();
    uint32_t timecode_scale = read_raw<uint32_t>(data, data_size);
    if (timecode_scale != default_timecode_scale) {
        cerr << "WARN: timecode scale is not " << default_timecode_scale
             << endl;
    }
    /* because it's timescode scale, we need to transform it to
     * mp4's timescale */
    return 1000000000 / timecode_scale;
  } else {
    cerr << "WARN: timecode scale not found. use default for now"
         << endl;
    return 1000000000 / default_timecode_scale;
  }
}

uint32_t WebmInfo::get_bitrate(const uint32_t timescale,
                               const uint32_t duration)
{
  if (duration == 0) {
    throw runtime_error("Duration cannot be zero");
  }
  double total_size = 0;
  auto elems = parser_.find_all(ElementTagID::SimpleBlock);
  for (const auto & elem : elems) {
    /* ignore the header size, which is much smaller than the actual size */
    total_size += elem->size();
  }
  double raw_bitrate = total_size / duration * timescale * 8;
  uint32_t bitrate = static_cast<uint32_t>(raw_bitrate);
  return (bitrate / 100) * 100;
}

uint32_t WebmInfo::get_duration(uint32_t timescale)
{
  /* get duration from TAG */
  auto tags = parser_.find_all(ElementTagID::SimpleTag);
  for (const auto tag : tags) {
    auto name_tag = tag->find_first(ElementTagID::TagName);
    if (name_tag) {
      string tag_name = name_tag->value();
      if (tag_name == "DURATION") {
        /* actual duration in seconds */
        auto time_tag = tag->find_first(ElementTagID::TagString);
        if (!time_tag) {
          throw runtime_error("Tag string not found for DURATION tag");
        }
        string time = time_tag->value();
        /* parse the actual time string */
        int hour, minute;
        float seconds;
        sscanf(time.c_str(), "%2d:%2d:%f", &hour, &minute, &seconds);
        float total_seconds = seconds + minute * 60 + hour * 3600;
        float duration = total_seconds * timescale;
        return static_cast<uint32_t>(duration);
      }
    }
  }
  return 0;
}

uint32_t WebmInfo::get_sample_rate()
{
  auto elm = parser_.find_first(ElementTagID::SamplingFrequency);
  if (elm) {
    uint32_t data_size = elm->size();
    string data = elm->value();
    if (data_size == 4) {
      float value = read_raw<float>(data, data_size);
      return static_cast<uint32_t>(value);
    } else if (data_size == 8) {
      double value = read_raw<double>(data, data_size);
      return static_cast<uint32_t>(value);
    } else {
      throw runtime_error("Invalid sampling frequency");
    }
  } else {
    return 0;
  }
}
