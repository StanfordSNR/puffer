#include <fcntl.h>
#include <unistd.h>
#include <endian.h>
#include <iostream>
#include <stdexcept>
#include <set>
#include <limits.h>

#include "exception.hh"
#include "mp4.hh"

using namespace std;
using namespace MP4;

MP4File::MP4File(const string & filename)
  : FileDescriptor(CheckSystemCall("open (" + filename + ")",
                                   open(filename.c_str(), O_RDONLY)))
{}

int64_t MP4File::seek(const int64_t offset, const int whence)
{
  return CheckSystemCall("lseek", lseek(fd_num(), offset, whence));
}

int64_t MP4File::curr_offset()
{
  return seek(0, SEEK_CUR);
}

int64_t MP4File::inc_offset(const int64_t offset)
{
  return seek(offset, SEEK_CUR);
}

int64_t MP4File::filesize()
{
  const int64_t prev_offset = curr_offset();
  const int64_t fsize = seek(0, SEEK_END);

  /* seek back to the previous offset */
  seek(prev_offset, SEEK_SET);

  return fsize;
}

void MP4File::reset()
{
  seek(0, SEEK_SET);
  set_eof(false);
}

uint8_t MP4File::read_uint8()
{
  string data = read(1);
  const uint8_t * size = reinterpret_cast<const uint8_t *>(data.c_str());
  return *size;
}

uint16_t MP4File::read_uint16()
{
  string data = read(2);
  const uint16_t * size = reinterpret_cast<const uint16_t *>(data.c_str());
  return be16toh(*size);
}

uint32_t MP4File::read_uint32()
{
  string data = read(4);
  const uint32_t * size = reinterpret_cast<const uint32_t *>(data.c_str());
  return be32toh(*size);
}

uint64_t MP4File::read_uint64()
{
  string data = read(8);
  const uint64_t * size = reinterpret_cast<const uint64_t *>(data.c_str());
  return be64toh(*size);
}

tuple<uint8_t, uint32_t> MP4File::read_version_flags()
{
  uint32_t data = read_uint32();
  return make_tuple((data >> 24) & 0xFF, data & 0x00FFFFFF);
}

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

shared_ptr<Box> Box::find_in_descendants(const string & type)
{
  for (const auto & child : children_) {
    if (child->type() == type) {
      return child;
    }

    auto found = child->find_in_descendants(type);
    if (found != nullptr) {
      return found;
    }
  }

  return nullptr;
}

void Box::print_structure(int indent)
{
  cout << string(indent, ' ') << "- " << type_ << " " << size_ << endl;

  for (const auto & child : children_) {
    child->print_structure(indent + 2);
  }
}

void Box::parse_data(MP4File & mp4, const int64_t data_size)
{
  /* simply ignore data by incrementing file offset */
  mp4.inc_offset(data_size);
}

MvhdBox::MvhdBox(const uint64_t size, const std::string & type)
  : Box(size, type), version_(), flags_(), creation_time_(),
    modification_time_(), timescale_(), duration_()
{}

void MvhdBox::print_structure(int indent)
{
  cout << string(indent, ' ') << "- " << type() << " " << size() << endl;

  string indent_str = string(indent + 2, ' ') + "| ";

  cout << indent_str << "timescale " << timescale_ << endl;
  cout << indent_str << "duration " << duration_ << endl;
}

void MvhdBox::parse_data(MP4File & mp4, const int64_t data_size)
{
  const int64_t init_offset = mp4.curr_offset();

  tie(version_, flags_) = mp4.read_version_flags();

  if (version_ == 1) { /* version 1 */
    creation_time_ = mp4.read_uint64();
    modification_time_ = mp4.read_uint64();
    timescale_ = mp4.read_uint32();
    duration_ = mp4.read_uint64();
  } else { /* version 0 */
    creation_time_ = mp4.read_uint32();
    modification_time_ = mp4.read_uint32();
    timescale_ = mp4.read_uint32();
    duration_ = mp4.read_uint32();
  }

  int64_t bytes_parsed = mp4.curr_offset() - init_offset;
  if (data_size < bytes_parsed) {
    throw runtime_error("MvhdBox::parse_data(): data size is too small");
  }

  /* skip parsing the rest of box */
  mp4.inc_offset(data_size - bytes_parsed);
}

SidxBox::SidxBox(const uint64_t size, const std::string & type)
  : Box(size, type), version_(), flags_(), reference_id_(),
    timescale_(), earlist_presentation_time_(), first_offset_(),
    reserved_(), reference_list_()
{}

void SidxBox::add_reference(SidxReference && ref)
{
  reference_list_.emplace_back(move(ref));
}

void SidxBox::print_structure(int indent)
{
  cout << string(indent, ' ') << "- " << type() << " " << size() << endl;

  string indent_str = string(indent + 2, ' ') + "| ";

  cout << indent_str << "reference id " << reference_id_ << endl;
  cout << indent_str << "timescale " << timescale_ << endl;

  cout << indent_str << "segment durations";
  for (const auto & ref : reference_list_) {
    cout << " " << ref.segment_duration;
  }
  cout << endl;
}

void SidxBox::parse_data(MP4File & mp4, const int64_t data_size)
{
  const int64_t init_offset = mp4.curr_offset();

  tie(version_, flags_) = mp4.read_version_flags();
  reference_id_ = mp4.read_uint32();
  timescale_ = mp4.read_uint32();

  if (version_ == 0) { /* version == 0 */
    earlist_presentation_time_ = mp4.read_uint32();
    first_offset_ = mp4.read_uint32();
  } else {
    earlist_presentation_time_ = mp4.read_uint64();
    first_offset_ = mp4.read_uint64();
  }

  reserved_ = mp4.read_uint16();

  uint16_t reference_count = mp4.read_uint16();
  for (unsigned int i = 0; i < reference_count; ++i) {
    uint32_t data = mp4.read_uint32();
    bool reference_type = (data >> 31) & 1;
    uint32_t reference_size = data & 0x7FFFFFFF;

    uint32_t segment_duration = mp4.read_uint32();

    data = mp4.read_uint32();
    bool starts_with_SAP = (data >> 31) & 1;
    uint8_t SAP_type = (data >> 28) & 7;
    uint32_t SAP_delta = (data >> 4) & 0x0FFFFFFF;

    add_reference({
      reference_type, reference_size, segment_duration,
      starts_with_SAP, SAP_type, SAP_delta
    });
  }

  int64_t bytes_parsed = mp4.curr_offset() - init_offset;
  if (data_size < bytes_parsed) {
    throw runtime_error("SidxBox::parse_data(): data size is too small");
  }

  /* skip parsing the rest of box */
  mp4.inc_offset(data_size - bytes_parsed);
}

StsdBox::StsdBox(const uint64_t size, const string & type):
  Box(size, type), version_(), flags_()
{}

void StsdBox::parse_data(MP4File & file, const int64_t data_size)
{
  const int64_t init_offset = file.curr_offset();

  tie(version_, flags_) = file.read_version_flags(); 
  
  uint32_t num_of_boxes = file.read_uint32();
  for(uint32_t i = 0; i < num_of_boxes; i++) {
    uint32_t size = file.read_uint32();
    string code = file.read(4);
    const int64_t pos = file.curr_offset();
    uint32_t box_size = size - 8;
    shared_ptr<Box> box;
    if(code == "avc1")
      box = make_shared<AVC1>(box_size, code);
    else
      box = make_shared<Box>(box_size, code);
    box->parse_data(file, box_size);
    add_child(move(box));

    /* clean up for un finished parsing */
    file.inc_offset(pos + box_size - file.curr_offset());
  }
  int64_t pos = file.curr_offset();
  if (pos == init_offset + data_size - 4)
    file.read_uint32(); /* optional padding */
  pos = file.curr_offset();
  if(pos != init_offset + data_size)
    throw runtime_error("Invalid stsd box");
}

VisualSampleEntry::VisualSampleEntry(const uint32_t data_size,
    const string & type): SampleEntry(data_size, type), width_(), 
  height_(), compressorname_()
{}

void VisualSampleEntry::parse_data(MP4File & file, const int64_t data_size)
{
  SampleEntry::parse_data(file, data_size);

  uint32_t unused1 = file.read_uint32();
  uint64_t unused2 = file.read_uint64();
  uint32_t unused3 = file.read_uint32();
  
  if(unused1 != 0 and unused2 != 0 and unused3 != 0)
    throw runtime_error("Invalid VisualSampleEntry");

  width_ = file.read_uint16();
  height_ = file.read_uint16();
  horizresolution_ = file.read_uint32();
  vertresolution_ = file.read_uint32();

  /* reserved */
  file.read_uint32();
  frame_count_ = file.read_uint16();
  uint8_t count = file.read_uint8();
  compressorname_ = file.read(32 - count - 1);
  depth_ = file.read_uint16();

  pre_defined_ = file.read_uint16();
}

AVC1::AVC1(const uint32_t data_size, const string & type):
  VisualSampleEntry(data_size, type), avc_profile_(),
  avc_profile_compatibility_(), avc_level_(), avcc_size_()
{}

void AVC1::parse_data(MP4File & file, const int64_t size)
{
  VisualSampleEntry::parse_data(file, size);
  /* please note that avcc and avc1 is implemented together
   */
  /* read avcc size */
  avcc_size_ = file.read_uint32();
  string avcc = file.read(4);
  if(avcc != "avcC")
    throw runtime_error("AVCC does not follow AVC1 immediately. Get " + avcc);
  configuration_version_ = file.read_uint8();
  avc_profile_ = file.read_uint8();
  avc_profile_compatibility_ = file.read_uint8();
  avc_level_ = file.read_uint8();
}

void AVC1::print_structure(int indent)
{
  cout << string(indent, ' ') << "- " << type() << " " << size() << endl;

  string indent_str = string(indent + 2, ' ') + "| ";

  cout << indent_str << "width " << width() << endl;
  cout << indent_str << "height " << height() << endl;
  
  cout << string(indent + 4, ' ') << "- " << "avcC" << " " << 
    avcc_size_ << endl;

  indent_str = string(indent + 4, ' ') + "| ";
  
  cout << indent_str << "avc_profile " << unsigned(avc_profile_) << endl;
  cout << indent_str << "avc_level " << unsigned(avc_level_) << endl;
}

Parser::Parser(const string & filename)
  : file_(filename), box_(make_shared<Box>(0, "MP4"))
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

void Parser::box_factory(shared_ptr<Box> & box, const string & type,
    const uint32_t & size, MP4File & file, const int64_t data_size)
{
  if (type == "mvhd") {
    box = make_shared<MvhdBox>(size, type);
  } else if (type == "sidx") {
    box = make_shared<SidxBox>(size, type);
  } else if (type == "stsd") {
    box = make_shared<StsdBox>(size, type);
  } else if (type == "avc1") {
    box = make_shared<AVC1>(size, type);
  } else {
    box = make_shared<Box>(size, type);
  }

  box->parse_data(file, data_size);
}

void Parser::create_boxes(const shared_ptr<Box> & parent_box,
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
      /* parse a container box recursively */
      auto box = make_shared<Box>(size, type);

      create_boxes(box, file_.curr_offset(), data_size);

      parent_box->add_child(move(box));
    } else {
      /* parse a regular box */
      shared_ptr<Box> box;
      box_factory(box, type, size, file_, data_size);
      parent_box->add_child(move(box));
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
  char media_seg[NAME_MAX];

  int size_copied = snprintf(media_seg, NAME_MAX,
                             media_seg_template.c_str(), curr_seg_number);

  if (size_copied < 0 or (size_t) size_copied >= NAME_MAX) {
    throw runtime_error("snprintf: resulting filename is too long");
  }

  return string(media_seg, (size_t) size_copied);
}
