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
#include "mdhd_box.hh"
#include "elst_box.hh"
#include "ctts_box.hh"
#include "stco_box.hh"
#include "stsc_box.hh"
#include "stss_box.hh"
#include "stts_box.hh"

using namespace std;
using namespace MP4;

MP4Parser::MP4Parser()
  : mp4_(), root_box_(make_shared<Box>("root")), ignored_boxes_()
{}

MP4Parser::MP4Parser(const string & mp4_file)
  : mp4_(make_shared<MP4File>(mp4_file, O_RDONLY)),
    root_box_(make_shared<Box>("root")), ignored_boxes_()
{}

void MP4Parser::parse()
{
  if (mp4_ == nullptr) {
    throw runtime_error("MP4Parser did not open an MP4 file to parse");
  }

  create_boxes(root_box_, 0, mp4_->filesize());
}

void MP4Parser::ignore_box(const string & type)
{
  ignored_boxes_.insert(type);
}

bool MP4Parser::is_ignored(const string & type)
{
  return ignored_boxes_.find(type) != ignored_boxes_.end();
}

shared_ptr<Box> MP4Parser::find_first_box_of(const string & type)
{
  if (type == "root") {
    return nullptr; /* do not return root box publicly */
  }

  return do_find_first_box_of(root_box_, type);
}

bool MP4Parser::is_video()
{
  return find_first_box_of("avc1") != nullptr;
}

bool MP4Parser::is_audio()
{
  return find_first_box_of("mp4a") != nullptr;
}

void MP4Parser::print_structure()
{
  for (auto it = root_box_->children_begin();
       it != root_box_->children_end(); ++it) {
    (*it)->print_box();
  }
}

void MP4Parser::add_top_level_box(shared_ptr<Box> && top_level_box)
{
  root_box_->add_child(move(top_level_box));
}

void MP4Parser::save_to_mp4(MP4File & mp4)
{
  for (auto it = root_box_->children_begin();
       it != root_box_->children_end(); ++it) {
    (*it)->write_box(mp4);
  }
}

shared_ptr<Box> MP4Parser::box_factory(const uint64_t size,
                                       const string & type,
                                       const uint64_t data_size)
{
  shared_ptr<Box> box;

  if (is_ignored(type)) {
    /* skip parsing box but save raw data */
    box = make_shared<Box>(size, type);
  } else {
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
    } else if (type == "stco") {
      box = make_shared<StcoBox>(size, type);
    } else if (type == "stsc") {
      box = make_shared<StscBox>(size, type);
    } else if (type == "stss") {
      box = make_shared<StssBox>(size, type);
    } else if (type == "stts") {
      box = make_shared<SttsBox>(size, type);
    } else if (type == "stsd") {
      auto stsd_box = make_shared<StsdBox>(size, type);

      /* special case: a sample entry box of stsd can be ignored too */
      if (is_ignored("avc1")) {
        stsd_box->ignore_sample_entry("avc1");
      }
      if (is_ignored("mp4a")) {
        stsd_box->ignore_sample_entry("mp4a");
      }

      box = move(stsd_box);
    } else {
      /* unknown box type */
      box = make_shared<Box>(size, type);
    }
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
