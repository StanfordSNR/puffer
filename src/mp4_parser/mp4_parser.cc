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

  /* create the initial segment */
  auto it = box_->children_begin();
  int64_t size_to_copy = 0;

  /* a valid initial segment must contain ftyp and moov boxes */
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

    size_to_copy += (*it)->size();
    it = next(it);
  }

  if (not (has_ftyp and has_moov)) {
    throw runtime_error("initial segment must contain ftyp and moov");
  }

  /* copy from MP4 and write to init_seg */
  copy_to_file(init_seg, size_to_copy);

  /* create one or more media segments */
  unsigned int curr_seg_number = start_number;

  while (it != box_->children_end()) {
    if ((*it)->type() != "moof") {
      break;
    }

    size_to_copy = (*it)->size();
    it = next(it);

    if ((*it)->type() != "mdat") {
      throw runtime_error("media segments must contain moof and mdat boxes");
    }

    size_to_copy += (*it)->size();
    it = next(it);

    /* populate segment template */
    string media_seg = populate_template(media_seg_template, curr_seg_number);

    /* copy from MP4 and write to media_seg */
    copy_to_file(media_seg, size_to_copy);

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

      /* recursively parse boxes in the container box */
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

void Parser::copy_to_file(const string & output_filename,
                          const int64_t size_to_copy)
{
  FileDescriptor output_fd(
    CheckSystemCall("open (" + output_filename + ")",
                    open(output_filename.c_str(), O_WRONLY | O_CREAT, 0644)));

  int64_t size_copied = 0;

  while (size_copied < size_to_copy) {
    string data = file_.read(size_to_copy - size_copied);
    if (file_.eof()) {
      throw runtime_error("copy_to_file: reached EOF before finishing copy");
    }

    size_copied += data.size();
    output_fd.write(data, true);
  }

  output_fd.close();
  cerr << "Created " << output_filename << endl;
}

string Parser::populate_template(const string & media_seg_template,
                                 const unsigned int curr_seg_number)
{
  const size_t max_filesize = 100;
  char media_seg[max_filesize];

  int size_copied = snprintf(media_seg, max_filesize,
                             media_seg_template.c_str(), curr_seg_number);

  if (size_copied < 0 or (size_t) size_copied >= max_filesize) {
    throw runtime_error("snprintf: resulting filename is too long");
  }

  return string(media_seg, (size_t) size_copied);
}
