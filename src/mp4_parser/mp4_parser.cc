#include <iostream>
#include <stdexcept>
#include <set>

#include "mp4_parser.hh"

using namespace std;
using namespace MP4;

Parser::Parser(const string & filename)
  : file_(filename), box_(make_unique<Box>(0, "MP4"))
{}

void Parser::parse()
{
  create_boxes(box_, 0, file_.filesize());
}

void Parser::print_structure()
{
  for (auto it = box_->children_begin(); it != box_->children_end(); ++it) {
    (*it)->print_structure();
  }
}

void Parser::create_boxes(unique_ptr<Box> & parent_box,
                          const int64_t start_offset, const int64_t total_size)
{
  while (true) {
    uint32_t size = file_.read_uint32();
    string type = file_.read(4);
    int64_t data_size;

    if (size == 1) {
      data_size = file_.read_uint64() - 16;
    } else {
      data_size = size - 8;
    }

    set<string> container_boxes {
      "dinf", "edts", "ipro", "mdia", "meta", "mfra", "moof", "moov", "mvex",
      "sinf", "skip", "stbl", "traf", "trak"};

    if (container_boxes.find(type) != container_boxes.end()) {
      auto new_container_box = make_unique<Box>(size, type);
      create_boxes(new_container_box, file_.curr_offset(), data_size);
      parent_box->add_child(move(new_container_box));
    } else {  /* non-container boxes */
      auto new_regular_box = make_unique<Box>(size, type);
      file_.inc_offset(data_size);  /* ignore data */
      parent_box->add_child(move(new_regular_box));
    }

    if (file_.curr_offset() >= start_offset + total_size) {
      break;
    }
  }
}
