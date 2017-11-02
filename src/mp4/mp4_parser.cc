#include <limits.h>
#include <fcntl.h>
#include <endian.h>
#include <iostream>
#include <stdexcept>

#include "exception.hh"
#include "mp4_parser.hh"
#include "ftyp_box.hh"
#include "mvhd_box.hh"
#include "mfhd_box.hh"
#include "tfhd_box.hh"
#include "tkhd_box.hh"
#include "tfdt_box.hh"
#include "trex_box.hh"
#include "sidx_box.hh"
#include "stsd_box.hh"
#include "stsz_box.hh"
#include "trun_box.hh"
#include "tkhd_box.hh"
#include "mdhd_box.hh"
#include "elst_box.hh"
#include "ctts_box.hh"

using namespace std;
using namespace MP4;

MP4Parser::MP4Parser(const string & filename, const bool writer)
  : mp4_(writer ?
         make_shared<MP4File>(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644) :
         make_shared<MP4File>(filename, O_RDONLY)),
         root_box_(make_shared<Box>("root"))
{}

void MP4Parser::parse()
{
  create_boxes(root_box_, 0, mp4_->filesize());
}

shared_ptr<Box> MP4Parser::find_first_box_of(const string & type)
{
  if (type == "root") {
    return nullptr; /* do not return root box publicly */
  }

  return do_find_first_box_of(root_box_, type);
}

void MP4Parser::print_structure()
{
  for (auto it = root_box_->children_begin();
       it != root_box_->children_end(); ++it) {
    (*it)->print_box();
  }
}

void MP4Parser::split(const string & init_seg,
                      const string & media_seg_template,
                      const unsigned int start_number)
{
  /* reset file offset and eof */
  mp4_->reset();

  /* create the initial segment */
  auto it = root_box_->children_begin();
  uint64_t size_to_copy = 0;

  /* a valid initial segment must contain ftyp and moov boxes */
  bool has_ftyp = false, has_moov = false;

  while (it != root_box_->children_end()) {
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

  while (it != root_box_->children_end()) {
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

void MP4Parser::add_top_level_box(shared_ptr<Box> && top_level_box)
{
  root_box_->add_child(move(top_level_box));
}

void MP4Parser::save_mp4_and_close()
{
  for (auto it = root_box_->children_begin();
       it != root_box_->children_end(); ++it) {
    (*it)->write_box(*mp4_);
  }

  mp4_->close();
}

shared_ptr<Box> MP4Parser::box_factory(const uint64_t size,
                                       const string & type,
                                       const uint64_t data_size)
{
  shared_ptr<Box> box;

  if (type == "ftyp" or type == "styp") {
    box = make_shared<FtypBox>(size, type);
  } else if (type == "mvhd") {
    box = make_shared<MvhdBox>(size, type);
  } else if (type == "mfhd") {
    box = make_shared<MfhdBox>(size, type);
  } else if (type == "tfhd") {
    box = make_shared<TfhdBox>(size, type);
  } else if (type == "sidx") {
    box = make_shared<SidxBox>(size, type);
  } else if (type == "trex") {
    box = make_shared<TrexBox>(size, type);
  } else if (type == "stsz") {
    box = make_shared<StszBox>(size, type);
  } else if (type == "tkhd") {
    box = make_shared<TkhdBox>(size, type);
  } else if (type == "trun") {
    box = make_shared<TrunBox>(size, type);
  } else if (type == "mdhd") {
    box = make_shared<MdhdBox>(size, type);
  } else if (type == "tfdt") {
    box = make_shared<TfdtBox>(size, type);
  } else if (type == "elst") {
    box = make_shared<ElstBox>(size, type);
  } else if (type == "ctts") {
    box = make_shared<CttsBox>(size, type);
  } else if (type == "stsd") {
    box = make_shared<StsdBox>(size, type);
  } else {
    box = make_shared<Box>(size, type);
  }

  uint64_t init_offset = mp4_->curr_offset();

  box->parse_data(*mp4_, data_size);

  if (mp4_->curr_offset() != init_offset + data_size) {
    throw runtime_error("parse_data() should increment offset by data_size");
  }

  return box;
}

void MP4Parser::create_boxes(const shared_ptr<Box> & parent_box,
                             const uint64_t start_offset,
                             const uint64_t total_size)
{
  while (true) {
    uint64_t size = mp4_->read_uint32();
    string type = mp4_->read(4);
    uint64_t data_size;

    if (size == 0) {
      data_size = start_offset + total_size - mp4_->curr_offset();
      size = data_size + 8;
    } else if (size == 1) {
      size = mp4_->read_uint64();
      data_size = size - 16;
    } else {
      data_size = size - 8;
    }

    if (type == "uuid") {
      mp4_->read(16); /* ignore extended_type */
    }

    if (mp4_container_boxes.find(type) != mp4_container_boxes.end()) {
      /* parse a container box recursively */
      auto box = make_shared<Box>(size, type);
      create_boxes(box, mp4_->curr_offset(), data_size);

      parent_box->add_child(move(box));
    } else {
      /* parse a regular box */
      shared_ptr<Box> box = box_factory(size, type, data_size);

      parent_box->add_child(move(box));
    }

    if (mp4_->curr_offset() >= start_offset + total_size) {
      break;
    }
  }
}

shared_ptr<Box> MP4Parser::do_find_first_box_of(const shared_ptr<Box> & box,
                                                const string & type)
{
  if (box->type() == type) {
    return box;
  }

  for (auto it = box->children_begin(); it != box->children_end(); ++it) {
    auto found = do_find_first_box_of(*it, type);
    if (found != nullptr) {
      return found;
    }
  }

  return nullptr;
}

void MP4Parser::copy_to_file(const string & output_filename,
                             const uint64_t size_to_copy)
{
  MP4File output_mp4(output_filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);

  uint64_t size_copied = 0;

  while (size_copied < size_to_copy) {
    string data = mp4_->read(size_to_copy - size_copied);
    if (mp4_->eof()) {
      throw runtime_error("copy_to_file: reached EOF before finishing copy");
    }

    size_copied += data.size();
    output_mp4.write(data);
  }

  output_mp4.close();
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
