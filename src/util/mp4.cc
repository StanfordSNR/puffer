#include <fcntl.h>
#include <unistd.h>
#include <endian.h>
#include <iostream>
#include <stdexcept>
#include <set>

#include "exception.hh"
#include "file_descriptor.hh"
#include "mp4.hh"


using namespace std;
using namespace MP4;

Box::Box(const uint64_t size, const string & type)
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

MVHD::MVHD(const uint64_t size, const string & type, MP4File * mp4)
  : MVHD(size, type)
{
  /* we need to reset the file offset so that
   * the parser can ignore this parsed boxes */
  int offset = 4;
  uint32_t temp =  mp4->read_uint32();
  this-> version_ = (temp >> 24) & 0xFF;
  this->flags_ = temp & 0x00FFFFFF;
  if(this->version_) {
    /* version 1 */
    this->creation_time_ = mp4->read_uint64();
    this->modification_time_ = mp4->read_uint64();
    this->timescale_ = mp4->read_uint32();
    this->duration_ = mp4->read_uint32();
    offset += 4 + 8 * 3;
  } else {
    /* version 0 */
    this->creation_time_ = mp4->read_uint32();
    this->modification_time_ = mp4->read_uint32();
    this->timescale_ = mp4->read_uint32();
    this->duration_ = mp4->read_uint32();
    offset += 4 * 4;
  }
  mp4->inc_offset(-offset);
  /* ignore the reset of them. our job is done here */
}

void MVHD::print_structure(int indent)
{
  /* we're sure that this does not have any child */
  cout << string(indent, ' ');
  cout << "- " << type() << " " << size() << " ";
  cout << "timescale " << this->timescale() << " ";
  cout << "duration " << this->duration() << endl;
}

SIDX::SIDX(const uint64_t size, const string & type, MP4File * mp4)
  : SIDX(size, type)
{
  int bytes_read = 0;
  uint32_t temp = mp4->read_uint32();
  this->version_ = (temp >> 24) & 0xFF;
  this->flags_ = temp & 0x00FFFFFF;
  this->reference_ID_ = mp4->read_uint32();
  this->timescale_ = mp4->read_uint32();
  /* to this point, we have 4*3 bytes */
  bytes_read += 4 * 3;
  if(!this->version_) {
    /* version == 0 */
    this->earlist_presentation_time_ = mp4->read_uint32();
    this->first_offset_ = mp4->read_uint32();
    bytes_read += 4 * 2;
  } else {
    this->earlist_presentation_time_ = mp4->read_uint64();
    this->first_offset_ = mp4->read_uint64();
    bytes_read += 8 * 2;
  }
  this->reserved_ = mp4->read_uint16();
  uint16_t reference_count = mp4->read_uint16();
  bytes_read += 4;

  for(int i = 0 ; i < reference_count; i++) {
    temp = mp4->read_uint32();
    bool reference_type = (temp >> 31) & 1;
    uint32_t reference_size = temp & 0x7FFFFFFF;
    uint32_t segment_duration = mp4->read_uint32();
    temp = mp4->read_uint32();
    bool starts_with_SAP = (temp >> 31) & 1;
    uint8_t SAP_type = (temp >> 28) & 7;
    uint32_t SAP_delta = (temp >> 4) & 0x0FFFFFFF;
    struct SidxReference ref {
      reference_type, reference_size, segment_duration,
      starts_with_SAP, SAP_type, SAP_delta};
    this->add_reference(ref);
    bytes_read += 3 * 4;
  }
  mp4->inc_offset(-bytes_read);
}

void SIDX::print_structure(int indent)
{
  cout << string(indent, ' ');
  cout << "- " << type() << " " << size() << endl;
  cout << string(indent + 3, ' ');
  cout << "reference ID " << reference_ID_ << " timescale " <<
    timescale_ << endl;
  for(auto ref: reference_list_) {
    cout << string(indent + 3, ' ');
    cout << " - segment duration " << ref.segment_duration << endl;
  }
}

MP4File::MP4File(const string & filename)
  : FileDescriptor(CheckSystemCall("open (" + filename + ")",
                                   open(filename.c_str(), O_RDONLY)))
{}

inline int64_t MP4File::seek(int64_t offset, int whence)
{
  return CheckSystemCall("lseek", lseek(fd_num(), offset, whence));
}

int64_t MP4File::curr_offset()
{
  return seek(0, SEEK_CUR);
}

int64_t MP4File::inc_offset(int64_t offset)
{
  return seek(offset, SEEK_CUR);
}

int64_t MP4File::filesize()
{
  int64_t prev_offset = curr_offset();
  int64_t fsize = seek(0, SEEK_END);

  /* seek back to the previous offset */
  seek(prev_offset, SEEK_SET);

  return fsize;
}

uint32_t MP4File::read_uint32()
{
  string data = read(4);
  const uint32_t * size = reinterpret_cast<const uint32_t *>(data.c_str());
  return be32toh(*size);
}

uint16_t MP4File::read_uint16()
{
  string data = read(2);
  const uint16_t * size = reinterpret_cast<const uint16_t *>(data.c_str());
  return be16toh(*size);
}

uint64_t MP4File::read_uint64()
{
  string data = read(8);
  const uint64_t * size = reinterpret_cast<const uint64_t *>(data.c_str());
  return be64toh(*size);
}

void MP4File::reset()
{
  seek(0, SEEK_SET);
  set_eof(false);
}

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
      unique_ptr<Box> new_regular_box;
      if(!type.compare("mvhd")) {
        new_regular_box = make_unique<MVHD>(size, type, &file_);
      } else if(!type.compare("sidx")) {
        new_regular_box = make_unique<SIDX>(size, type, &file_);
      } else {
        new_regular_box = make_unique<Box>(size, type);
      }
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
