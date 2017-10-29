#include <iostream>
#include <stdexcept>

#include "box.hh"

using namespace std;
using namespace MP4;

Box::Box(const uint64_t size, const string & type)
  : size_(size), type_(type), children_()
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

shared_ptr<Box> Box::find_descendant(const string & type)
{
  for (const auto & child : children_) {
    if (child->type() == type) {
      return child;
    }

    auto found = child->find_descendant(type);
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
  mp4.inc_offset(data_size);
}

void Box::skip_data(MP4File & mp4, const uint64_t data_size,
                    const uint64_t init_offset)
{
  uint64_t data_parsed = mp4.curr_offset() - init_offset;

  if (data_size < data_parsed) {
    throw runtime_error(type() + " box::skip_data(): data size is too small");
  } else if (data_size > data_parsed) {
    mp4.inc_offset(data_size - data_parsed);
  }
}
