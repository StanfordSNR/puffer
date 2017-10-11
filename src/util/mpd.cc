#include "mpd.hh"
#include <sstream>
#include <stack>
#include <set>
#include <iostream>
#include <fstream>
#include <memory>
#include <time.h>
#include <string>

using namespace std;

#define XML_HEADER "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
#define XML_INDENT "  "

XMLWriter::XMLWriter(): tag_open_(false), newline_( true),
  os_(ostringstream()), elt_stack_(stack<XMLNode>())
{
  this->os_ << (XML_HEADER) << endl;
}

XMLWriter::~XMLWriter()
{}

XMLWriter& XMLWriter::open_elt(const char *tag)
{
  this->close_tag();
  if (this->elt_stack_.size() > 0) {
    this->os_ << endl;
    this->indent();
    this->elt_stack_.top().hasContent = true;
  }
  this-> os_ << "<" << tag;
  this->elt_stack_.push(XMLNode( tag));
  this->tag_open_ = true;
  this->newline_ = false;

  return *this;
}

XMLWriter& XMLWriter::close_elt()
{
  if (!this->elt_stack_.size())
    throw "XMLWriter is in an incorrect state.";
  XMLNode node = this->elt_stack_.top();
  this->elt_stack_.pop();
  if (!node.hasContent) {
    // no actual value, maybe just attr
    this->os_ << " />";
    this->tag_open_ = false;
  } else {
    this->close_tag();
    if (this->newline_) {
      os_ << endl;
      this->indent();
    }
    this->os_ << "</" << node.tag_ << ">";
  }
  this->newline_ = true;
  return *this;
}

XMLWriter& XMLWriter::close_all()
{
  while (this->elt_stack_.size())
    this->close_elt();
  return *this;
}

XMLWriter& XMLWriter::attr(const char *key, const char *val)
{
  this->os_ << " " << key << "=\"";
  this->write_escape(val);
  this->os_ << "\"";
  return *this;
}

XMLWriter& XMLWriter::attr(const char *key, std::string val)
{
  return this->attr(key, val.c_str());
}

XMLWriter& XMLWriter::attr(const char *key, unsigned int val)
{
  return this->attr(key, to_string(val));
}

XMLWriter& XMLWriter::attr(const char *key, int val)
{
  return this->attr(key, to_string(val));
}

XMLWriter& XMLWriter::content(const char *val)
{
  this->close_tag();
  this->write_escape(val);
  this->elt_stack_.top().hasContent = true;
  return *this;
}

XMLWriter& XMLWriter::content(const int val)
{
  return this->content(to_string(val));
}

XMLWriter& XMLWriter::content(const unsigned int val)
{
  return this->content(to_string(val));
}

XMLWriter& XMLWriter::content(const string & val)
{
  return this->content(val.c_str());
}

inline void XMLWriter::close_tag()
{
  if (this->tag_open_) {
    this-> os_ << ">";
    this->tag_open_ = false;
  }
}


inline void XMLWriter::indent()
{
  for (unsigned int i = 0; i < this->elt_stack_.size(); i++)
    this->os_ << (XML_INDENT);
}

inline void XMLWriter::write_escape(const char *str)
{
  for (; *str; str++)
    switch (*str) {
      case '&': this->os_ << "&amp;"; break;
      case '<': this->os_ << "&lt;"; break;
      case '>': this->os_ << "&gt;"; break;
      case '\'': this->os_ << "&apos;"; break;
      case '"': this->os_ << "&quot;"; break;
      default: this->os_.put(*str); break;
    }
}

string XMLWriter::str()
{
  return this->os_.str();
}

void XMLWriter::output(ofstream &out)
{
  out << this->str();
}


MPD::AdaptionSet::AdaptionSet(std::string init_uri, std::string media_uri,
    unsigned int framerate, unsigned int duration) : init_uri_(init_uri),
  media_uri_(media_uri), framerate_(framerate), duration_(duration),
  repr_set_(set<MPD::Representation>())
{}

MPD::AdaptionSet::~AdaptionSet()
{}


void MPD::AdaptionSet::add_repr(string id, unsigned int width, unsigned int height,
    unsigned int bitrate, unsigned int avc_level, MPD::MimeType type,
    unsigned int framerate)
{
  if(this->framerate_ != framerate)
    throw "Multiple framerate in one adaption set is not supported";
  Representation repr = Representation {
    id, width, height, bitrate, MPD::ProfileLevel::Low,
    avc_level, type, framerate};
  this->repr_set_.insert(repr);
}

MPDWriter::MPDWriter(int64_t update_period, string title, string base_url):
  update_period_(update_period), title_(title),
  writer_(make_unique<XMLWriter>()), base_url_(base_url),
  adaption_set_(set<MPD::AdaptionSet>())
{}

MPDWriter::~MPDWriter()
{}

string MPDWriter::format_time_now()
{
  /* this is possible because C++ will convert char* into std:string
   * therefore no pointer to stack problem                        */
  time_t now= time(0);
  tm * now_tm= gmtime(&now);
  char buf[42];
  strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", now_tm);
  return buf;
}


string MPDWriter::write_codec(MPD::MimeType type, MPD::Representation & repr) {
  char buf[20];
  switch (type) {
    case MPD::MimeType::Video:
      switch (repr.profile) {
        case MPD::ProfileLevel::Low:
          sprintf(buf, "avc1.42E0%02X", repr.avc_level);
          break;
        case MPD::ProfileLevel::Main:
          sprintf(buf, "4D40%02X", repr.avc_level);
          break;
        case MPD::ProfileLevel::High:
          sprintf(buf, "6400%02x", repr.avc_level);
          break;
        default:
          throw "Unsupported AVC profile";
      break;
      }
    case MPD::MimeType::Audio:
      sprintf(buf, "mp4a.40.2");
      break;
    default:
      throw "Unsupported MIME type";
  }
  return buf;
}


/* THIS IS FOR VIDEO ONLY */
void MPDWriter::write_repr(MPD::Representation & repr)
{
  this->writer_->open_elt("Representation");
  this->writer_->attr("id", "v" + repr.id_);
  this->writer_->attr("mimeType", "video/mp4");
  string codec = this->write_codec(MPD::MimeType::Video, repr);
  this->writer_->attr("codecs", codec);
  this->writer_->attr("width", repr.width);
  this->writer_->attr("height", repr.height);
  this->writer_->attr("frameRate", repr.framerate_);
  this->writer_->attr("sar", "1:1");
  this->writer_->attr("startWithSAP", "1");
  this->writer_->attr("bandwidth", repr.bitrate);
  this->writer_->close_elt();
}

/* TODO: Video only
 * refactor this into subclass */
void MPDWriter::write_adaption_set(MPD::AdaptionSet & set) {
  this->writer_->open_elt("AdaptationSet");
  this->writer_->attr("segmentAlignment", "true");

  /* write the segment template */
  this->writer_->open_elt("SegmentTemplate");
  this->writer_->attr("timescale", set.framerate_);
  this->writer_->attr("duration", set.duration_ * set.framerate_); /* number of frames */
  this->writer_->attr("media", set.media_uri_);
  this->writer_->attr("startNumber", 1); /* the initial segment number */
  this->writer_->attr("initialization", set.init_uri_);
  this->writer_->close_elt();

  /* Write the segment */
  for(auto repr: set.repr_set_)
    this->write_repr(repr);

  this->writer_->close_elt();
}

string MPDWriter::flush()
{
  this->writer_->open_elt("MPD");
  /* MPD scheme */
  this->writer_->attr("xmlns:xsi", "http://www.w3.org/2001/XMLSchema-instance");
  this->writer_->attr("xmlns", "urn:mpeg:dash:schema:mpd:2011");
  this->writer_->attr("xmlns:xlink", "http://www.w3.org/1999/xlink");
  this->writer_->attr("xsi:schemaLocation",
      "urn:mpeg:DASH:schema:MPD:2011 http://standards.iso.org/ittf/PubliclyAvailableStandards/MPEG-DASH_schema_files/DASH-MPD.xsd");
  /* write the time when this MPD is flushed" */
  string t = this->format_time_now();
  this->writer_->attr("availabilityStartTime", t);
  this->writer_->attr("profiles", "urn:mpeg:dash:profile:isoff-live:2011");
  this->writer_->attr("type", "dynamic");
  //this->writer_->attr("mediaPresentationDuration", ""); // TODO
  this->writer_->attr("minBufferTime", ""); // TODO

  /* Base URL */
  this->writer_->open_elt("BaseURL");
  this->writer_->content(this->base_url_);
  this->writer_->close_elt();

  /* Title */
  this->writer_->open_elt("ProgramInformation");
  this->writer_->open_elt("Title");
  this->writer_->content(this->title_);
  this->writer_->close_elt();
  this->writer_->close_elt();

  /* Period */
  /* only 1 period for now */
  this->writer_->open_elt("Period");
  this->writer_->attr("id", "1");
  /* Adaption Set */
  for (auto it: this->adaption_set_) {
    this->write_adaption_set(it);
  }

  return this->writer_->str();
}
