#include "mp4_box.hh"

using namespace std;
using namespace MP4;

Box::Box(const uint32_t size, const string & type)
  : size_(size), type_(type), children_()
{}

string Box::get_type()
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
