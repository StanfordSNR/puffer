#include <iostream>
#include <stdexcept>
#include <set>
#include <fcntl.h>

#include "file_descriptor.hh"
#include "exception.hh"
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

void Parser::split(const std::string & init_seg,
                   const std::string & media_seg_template,
                   const unsigned int start_number)
{
  /* reset file offset and eof */
  file_.reset();

  /* create an initial segment */
  auto it = box_->children_begin();
  int64_t copy_size = 0;

  bool has_ftyp = false, has_moov = false;

  while (it != box_->children_end()) {
    string type = (*it)->type();

    if (type == "moof") {
      break;
    }

    if (type == "ftyp") {
      has_ftyp = true;
    } else if (type == "moov") {
      has_moov = true;
    }

    copy_size += (*it)->size();
    it = next(it);
  }

  if (not (has_ftyp and has_moov)) {
    throw runtime_error("initial segment must contain ftyp and moov");
  }

  /* copy from file_ to init_seg_fd */
  cerr << "Creating " << init_seg << endl;
  FileDescriptor init_seg_fd(
      CheckSystemCall("open (" + init_seg + ")",
                      open(init_seg.c_str(), O_WRONLY | O_CREAT, 0644)));
  copy_to_file(init_seg_fd, copy_size);
  init_seg_fd.close();

  /* create one or more media segments */
  unsigned int curr_seg_number = start_number;

  while (it != box_->children_end()) {
    if ((*it)->type() != "moof") {
      break;
    }

    copy_size = (*it)->size();
    it = next(it);

    if ((*it)->type() != "mdat") {
      throw runtime_error("no mdat after moof");
    }

    copy_size += (*it)->size();
    it = next(it);

    /* populate segment template */
    const size_t max_filesize = 100;
    char media_seg[max_filesize];
    int cp = snprintf(media_seg, max_filesize,
                      media_seg_template.c_str(), curr_seg_number);

    if (cp < 0 or (size_t) cp >= max_filesize) {
      throw runtime_error("snprintf: resulting filename is too long");
    }

    /* copy from file_ to media_seg_fd */
    cerr << "Creating " << string(media_seg) << endl;
    FileDescriptor media_seg_fd(
        CheckSystemCall("open (" + string(media_seg) + ")",
                        open(media_seg, O_WRONLY | O_CREAT, 0644)));
    copy_to_file(media_seg_fd, copy_size);
    media_seg_fd.close();

    curr_seg_number += 1;
  }
}

void Parser::create_boxes(unique_ptr<Box> & parent_box,
                          const int64_t start_offset, const int64_t total_size)
{
  while (true) {
    uint32_t size = file_.read_uint32();
    string type = file_.read(4);
    int64_t data_size;

    if (size == 0) {
      data_size = start_offset + total_size - file_.curr_offset();
      size = data_size + 8;
    } else if (size == 1) {
      data_size = file_.read_uint64() - 16;
    } else {
      data_size = size - 8;
    }

    set<string> container_boxes{
      "moov", "trak", "edts", "mdia", "minf", "stbl", "mvex", "moof", "traf",
      "mfra", "skip", "strk", "meta", "dinf", "ipro", "sinf", "fiin", "paen",
      "meco", "mere"};

    if (container_boxes.find(type) != container_boxes.end()) {
      auto new_container_box = make_unique<Box>(size, type);

      /* recursively create boxes */
      create_boxes(new_container_box, file_.curr_offset(), data_size);

      parent_box->add_child(move(new_container_box));
    } else {
      auto new_regular_box = make_unique<Box>(size, type);

      /* ignore data by incrementing file offset */
      file_.inc_offset(data_size);

      parent_box->add_child(move(new_regular_box));
    }

    if (file_.curr_offset() >= start_offset + total_size) {
      break;
    }
  }
}

void Parser::copy_to_file(FileDescriptor & output_fd, const int64_t copy_size)
{
  int64_t read_size = 0;

  while (read_size < copy_size) {
    string data = file_.read(copy_size - read_size);
    if (file_.eof()) {
      throw runtime_error("copy_to_file: reached EOF before finishing copy");
    }

    read_size += data.size();
    output_fd.write(data, true);
  }
}
