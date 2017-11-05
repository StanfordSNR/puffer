#include <iostream>
#include <stdexcept>

#include "box.hh"
#include "strict_conversions.hh"

using namespace std;
using namespace MP4;

Box::Box(const uint64_t size, const string & type)
  : size_(size), type_(type), raw_data_(), children_()
{}

Box::Box(const string & type)
  : size_(), type_(type), raw_data_(), children_()
{}

void Box::add_child(shared_ptr<Box> && child)
{
  children_.emplace_back(move(child));
}

void Box::remove_child(const string & type)
{
  for (auto it = children_.begin(); it != children_.end(); ) {
    if ((*it)->type() == type) {
      it = children_.erase(it);
      break;
    } else {
      ++it;
    }
  }
}

void Box::insert_child(shared_ptr<Box> && child, const string & type)
{
  for (auto it = children_.begin(); it != children_.end(); ) {
    if ((*it)->type() == type) {
      children_.insert(++it, move(child));
      break;
    } else {
      ++it;
    }
  }
}

shared_ptr<Box> Box::find_child(const string & type)
{
  for (const auto & child : children_) {
    if (child->type() == type) {
      return child;
    }
  }

  return nullptr;
}

list<shared_ptr<Box>>::const_iterator Box::children_begin()
{
  return children_.cbegin();
}

list<shared_ptr<Box>>::const_iterator Box::children_end()
{
  return children_.cend();
}

void Box::print_box(const unsigned int indent)
{
  print_size_type(indent);

  for (const auto & child : children_) {
    child->print_box(indent + 2);
  }
}

void Box::parse_data(MP4File & mp4, const uint64_t data_size)
{
  raw_data_ = mp4.read_exactly(narrow_cast<size_t>(data_size));
}

void Box::write_box(MP4File & mp4)
{
  uint64_t size_offset = mp4.curr_offset();

  write_size_type(mp4);

  if (raw_data_.size()) {
    mp4.write(raw_data_);
  } else if (children_.size()) {
    for (const auto & child : children_) {
      child->write_box(mp4);
    }
  }

  fix_size_at(mp4, size_offset);
}

void Box::print_size_type(const unsigned int indent)
{
  cout << string(indent, ' ') << "- " << type_ << " " << size_ << endl;
}

void Box::write_size_type(MP4File & mp4)
{
  /* does not support creating boxes with size > uint32 for now */
  mp4.write_uint32(narrow_cast<uint32_t>(size_));
  mp4.write_string(type_, 4);
}

void Box::fix_size_at(MP4File & mp4, const uint64_t size_offset)
{
  uint64_t curr_offset = mp4.curr_offset();
  uint32_t size = narrow_cast<uint32_t>(curr_offset - size_offset);

  mp4.write_uint32_at(size, size_offset);
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
    throw runtime_error(type() + " box: data remains to be parsed");
  }
}

FullBox::FullBox(const uint64_t size, const string & type)
  : Box(size, type), version_(), flags_()
{}

FullBox::FullBox(const string & type,
                 const uint8_t version, const uint32_t flags)
  : Box(type), version_(version), flags_(flags & 0x00FFFFFF)
{}

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
  uint32_t data = (version_ << 24) + (flags_ & 0x00FFFFFF);
  mp4.write_uint32(data);
}
