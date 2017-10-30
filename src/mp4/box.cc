#include <iostream>
#include <stdexcept>

#include "box.hh"
#include "strict_conversions.hh"

using namespace std;
using namespace MP4;

Box::Box(const uint64_t size, const string & type)
  : size_(size), type_(type), children_()
{}

Box::Box(const string & type)
  : size_(), type_(type), children_()
{}

void Box::add_child(shared_ptr<Box> && child)
{
  children_.emplace_back(move(child));
}

vector<shared_ptr<Box>>::const_iterator Box::children_begin()
{
  return children_.cbegin();
}

vector<shared_ptr<Box>>::const_iterator Box::children_end()
{
  return children_.cend();
}

shared_ptr<Box> Box::find_first_descendant_of(const string & type)
{
  for (const auto & child : children_) {
    if (child->type() == type) {
      return child;
    }

    auto found = child->find_first_descendant_of(type);
    if (found != nullptr) {
      return found;
    }
  }

  return nullptr;
}

void Box::print_structure(const unsigned int indent)
{
  cout << string(indent, ' ') << "- " << type_ << " " << size_ << endl;

  for (const auto & child : children_) {
    child->print_structure(indent + 2);
  }
}

void Box::parse_data(MP4File & mp4, const uint64_t data_size)
{
  /* ignore data by incrementing file offset */
  if (data_size > 0) {
    mp4.inc_offset(data_size);
  }
}

void Box::infer_size()
{
  if (size_ > 0) {
    return;
  }

  uint64_t size = 8;

  if (size_ == 1) {
    size += 8;
  }

  if (type_ == "uuid") {
    size += 2;
  }

  for (const auto & child : children_) {
    child->infer_size();
    size += child->size();
  }

  set_size(size);
}

void Box::write_box(MP4File & mp4)
{
  write_size_type(mp4);

  for (const auto & child : children_) {
    child->write_box(mp4);
  }
}

void Box::print_size_type(const unsigned int indent)
{
  cout << string(indent, ' ') << "- " << type_ << " " << size_ << endl;
}

void Box::write_size_type(MP4File & mp4)
{
  if (size_ == 1 or type_ == "uuid" or type_ == "mdat") {
    throw runtime_error("does not support writing special box headers");
  }

  mp4.write_uint32(narrow_cast<uint32_t>(size_));
  mp4.write_string(type_, 4);
}

void Box::skip_data_left(MP4File & mp4, const uint64_t data_size,
                         const uint64_t init_offset)
{
  uint64_t data_parsed = mp4.curr_offset() - init_offset;

  if (data_size < data_parsed) {
    throw runtime_error(type() + " box: data size is too small");
  } else if (data_size > data_parsed) {
    mp4.inc_offset(data_size - data_parsed);
  }
}

void Box::check_data_left(MP4File & mp4, const uint64_t data_size,
                          const uint64_t init_offset)
{
  if (mp4.curr_offset() != init_offset + data_size) {
    throw runtime_error("data remains to be parsed");
  }
}

FullBox::FullBox(const uint64_t size, const std::string & type)
  : Box(size, type), version_(), flags_()
{}

FullBox::FullBox(const std::string & type,
                 const uint8_t version, const uint32_t flags)
  : Box(type), version_(version), flags_(flags)
{}

void FullBox::parse_data(MP4File & mp4, const uint64_t data_size)
{
  int64_t init_offset = mp4.curr_offset();

  parse_version_flags(mp4);

  skip_data_left(mp4, data_size, init_offset);
}

void FullBox::print_version_flags(const unsigned int indent)
{
  string indent_str = string(indent, ' ') + "| ";
  cout << indent_str << "version " << unsigned(version_) << endl;
  cout << indent_str << "flags " << flags_ << endl;
}

void FullBox::parse_version_flags(MP4File & mp4)
{
  uint32_t data = mp4.read_uint32();
  version_ = (data >> 24) & 0xFF;
  flags_ = data & 0x00FFFFFF;
}


void FullBox::write_version_flags(MP4File & mp4)
{
  uint32_t data = (version_ << 24) + flags_;
  mp4.write_uint32(data);
}
