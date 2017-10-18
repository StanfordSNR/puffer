#include <iostream>
#include "mp4_box.hh"

using namespace std;
using namespace MP4;

Box::Box(const uint64_t size, const std::string & type)
  : size_(size), type_(type), children_()
{}

uint64_t Box::size()
{
  return size_;
}

string Box::type()
{
  return type_;
}

void Box::add_child(unique_ptr<Box> child)
{
  children_.emplace_back(move(child));
}

vector<unique_ptr<Box>>::iterator Box::children_begin()
{
  return children_.begin();
}

vector<unique_ptr<Box>>::iterator Box::children_end()
{
  return children_.end();
}

void Box::print_structure(int indent)
{
  cout << string(indent, ' ');
  cout << "- " << type_ << " " << size_ << endl;

  for (auto const & child : children_) {
    child->print_structure(indent + 2);
  }
}
