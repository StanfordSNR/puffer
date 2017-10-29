#include <iostream>
#include <stdexcept>
#include <limits.h>
#include <fcntl.h>
#include <endian.h>

#include "exception.hh"
#include "mp4_parser.hh"
#include "mvhd_box.hh"
#include "mfhd_box.hh"
#include "sidx_box.hh"
#include "stsd_box.hh"

using namespace std;
using namespace MP4;

MP4Parser::MP4Parser(const string & filename)
  : mp4_(filename), box_(make_shared<Box>(0, "MP4"))
{}

void MP4Parser::parse()
{
  create_boxes(box_, 0, mp4_.filesize());
}

void MP4Parser::print_structure()
{
  for (auto it = box_->children_begin(); it != box_->children_end(); ++it) {
    (*it)->print_structure();
  }
}

void MP4Parser::split(const std::string & init_seg,
                      const std::string & media_seg_template,
                      const unsigned int start_number)
{
  /* reset file offset and eof */
  mp4_.reset();

  /* create the initial segment */
  auto it = box_->children_begin();
  uint64_t size_to_copy = 0;

  /* a valid initial segment must contain ftyp and moov boxes */
  bool has_ftyp = false, has_moov = false;

  while (it != box_->children_end()) {
    string type = (*it)->type();

    if (type == "ftyp") {
      has_ftyp = true;
    } else if (type == "moov") {
      has_moov = true;
    } else if (type == "moof") {
      throw runtime_error("initial segment shall not have moof");
    }

    size_to_copy += (*it)->size();
    ++it;

    if (has_ftyp and has_moov) {
      break;
    }
  }

  if (not (has_ftyp and has_moov)) {
    throw runtime_error("initial segment must contain ftyp and moov");
  }

  /* copy from MP4 and write to init_seg */
  copy_to_file(init_seg, size_to_copy);

  /* create one or more media segments */
  unsigned int curr_seg_number = start_number;

  while (it != box_->children_end()) {
    string type = (*it)->type();

    if (type == "styp" or type == "sidx") {
      continue;
    } else if (type == "moof") {
      size_to_copy = (*it)->size();
      ++it;

      if ((*it)->type() != "mdat") {
        throw runtime_error("mdat must follow moof");
      }

      size_to_copy += (*it)->size();
      ++it;

      /* populate segment template */
      string media_seg = populate_template(media_seg_template, curr_seg_number);

      /* copy from MP4 and write to media_seg */
      copy_to_file(media_seg, size_to_copy);

      curr_seg_number += 1;
    } else {
      break;
    }
  }
}

shared_ptr<Box> MP4Parser::find_first_box_of(const string & type)
{
  return box_->find_first_descendant_of(type);
}

shared_ptr<Box> MP4Parser::box_factory(
    const uint64_t size, const string & type,
    MP4File & mp4, const uint64_t data_size)
{
  shared_ptr<Box> box;

  if (type == "mvhd") {
    box = make_shared<MvhdBox>(size, type);
  } else if (type == "mfhd") {
    box = make_shared<MfhdBox>(size, type);
  } else if (type == "sidx") {
    box = make_shared<SidxBox>(size, type);
  } else if (type == "stsd") {
    box = make_shared<StsdBox>(size, type);
  } else {
    box = make_shared<Box>(size, type);
  }

  uint64_t init_offset = mp4.curr_offset();

  box->parse_data(mp4, data_size);

  if (mp4.curr_offset() != init_offset + data_size) {
    throw runtime_error("parse_data() should increment offset by data_size");
  }

  return box;
}

void MP4Parser::create_boxes(const shared_ptr<Box> & parent_box,
                             const uint64_t start_offset,
                             const uint64_t total_size)
{
  while (true) {
    uint32_t size = mp4_.read_uint32();
    string type = mp4_.read(4);
    uint64_t data_size;

    if (size == 0) {
      data_size = start_offset + total_size - mp4_.curr_offset();
      size = data_size + 8;
    } else if (size == 1) {
      data_size = mp4_.read_uint64() - 16;
    } else {
      data_size = size - 8;
    }

    if (type == "uuid") {
      /* ignore extended_type */
      mp4_.read(16);
    }

    if (mp4_container_boxes.find(type) != mp4_container_boxes.end()) {
      /* parse a container box recursively */
      auto box = make_shared<Box>(size, type);
      create_boxes(box, mp4_.curr_offset(), data_size);

      parent_box->add_child(move(box));
    } else {
      /* parse a regular box */
      shared_ptr<Box> box = box_factory(size, type, mp4_, data_size);

      parent_box->add_child(move(box));
    }

    if (mp4_.curr_offset() >= start_offset + total_size) {
      break;
    }
  }
}

void MP4Parser::copy_to_file(const string & output_filename,
                             const uint64_t size_to_copy)
{
  FileDescriptor output_fd(
    CheckSystemCall("open (" + output_filename + ")",
                    open(output_filename.c_str(), O_WRONLY | O_CREAT, 0644)));

  uint64_t size_copied = 0;

  while (size_copied < size_to_copy) {
    string data = mp4_.read(size_to_copy - size_copied);
    if (mp4_.eof()) {
      throw runtime_error("copy_to_file: reached EOF before finishing copy");
    }

    size_copied += data.size();
    output_fd.write(data, true);
  }

  output_fd.close();
  cerr << "Created " << output_filename << endl;
}

string MP4Parser::populate_template(const string & media_seg_template,
                                    const unsigned int curr_seg_number)
{
  char media_seg[NAME_MAX];

  int size_copied = snprintf(media_seg, NAME_MAX,
                             media_seg_template.c_str(), curr_seg_number);

  if (size_copied < 0 or (size_t) size_copied >= NAME_MAX) {
    throw runtime_error("snprintf: resulting filename is too long");
  }

  return string(media_seg, (size_t) size_copied);
}
