#include "mpd.hh"
#include <sstream>
#include <stack>
#include <set>
#include <iostream>
#include <fstream>
#include <memory>
#include <time.h>
#include <string>
#include <limits>
#include <cmath>
#include "exception.hh"

#define XML_HEADER "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
#define XML_INDENT "  "

XMLWriter::XMLWriter(): tag_open_(false), newline_( true),
  os_(std::ostringstream()), elt_stack_(std::stack<XMLNode>())
{
  this->os_ << (XML_HEADER) << std::endl;
}

XMLWriter::~XMLWriter()
{}

XMLWriter& XMLWriter::open_elt(const char *tag)
{
  this->close_tag();
  if (this->elt_stack_.size() > 0) {
    this->os_ << std::endl;
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
    throw std::runtime_error("XMLWriter is in an incorrect state.");
  XMLNode node = this->elt_stack_.top();
  this->elt_stack_.pop();
  if (!node.hasContent) {
    /* no actual value, maybe just attr */
    this->os_ << " />";
    this->tag_open_ = false;
  } else {
    this->close_tag();
    if (this->newline_) {
      os_ << std::endl;
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
  return this->attr(key, std::to_string(val));
}

XMLWriter& XMLWriter::attr(const char *key, int val)
{
  return this->attr(key, std::to_string(val));
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
  return this->content(std::to_string(val));
}

XMLWriter& XMLWriter::content(const unsigned int val)
{
  return this->content(std::to_string(val));
}

XMLWriter& XMLWriter::content(const std::string & val)
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

std::string XMLWriter::str()
{
  return this->os_.str();
}

void XMLWriter::output(std::ofstream &out)
{
  out << this->str();
}


MPD::AdaptionSet::AdaptionSet(int id, std::string init_uri,
        std::string media_uri,
    unsigned int duration, unsigned int timescale, MPD::MimeType type):
  id(id), init_uri(init_uri), media_uri(media_uri),
    duration(duration), timescale(timescale), type(type),
    repr_set_(std::set<Representation*>())
{}

std::set<MPD::Representation*> MPD::AdaptionSet::get_repr_set()
{
  return this->repr_set_;
}

void MPD::AdaptionSet::add_repr_(MPD::Representation* repr)
{
  this->repr_set_.insert(repr);
}

MPD::VideoAdaptionSet::VideoAdaptionSet(int id, std::string init_uri,
        std::string media_uri, float framerate, unsigned int duration,
        unsigned int timescale): AdaptionSet(id, init_uri, media_uri,
            duration, timescale, MPD::MimeType::Video), framerate(framerate)
{}

MPD::AudioAdaptionSet::AudioAdaptionSet(int id, std::string init_uri,
    std::string media_uri, unsigned int duration, unsigned int
    timescale) : AdaptionSet(id, init_uri, media_uri, duration,
      timescale, MPD::MimeType::Audio)
{}

void MPD::AudioAdaptionSet::add_repr(AudioRepresentation * repr)
{
  MPD::Representation * r = (MPD::Representation*)repr;
  this->add_repr_(r);
}

void MPD::VideoAdaptionSet::add_repr(VideoRepresentation* repr)
{
  if(this->framerate!= repr->framerate)
    throw std::runtime_error("Multiple framerate in one adaption set\
is not supported");
  MPD::Representation * r = (MPD::Representation*)repr;
  this->add_repr_(r);
}

MPDWriter::MPDWriter(int64_t update_period, int64_t min_buffer_time,
    std::string base_url):
  update_period_(update_period), min_buffer_time_(min_buffer_time),
  writer_(std::make_unique<XMLWriter>()), base_url_(base_url),
  adaption_set_(std::set<MPD::AdaptionSet*>())
{}

MPDWriter::~MPDWriter()
{}

std::string MPDWriter::format_time_now()
{
  /* this is possible because C++ will convert char* into std:string
   * therefore no pointer to stack problem                        */
  time_t now= time(0);
  tm * now_tm= gmtime(&now);
  char buf[42];
  strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", now_tm);
  return buf;
}


std::string MPDWriter::write_codec(MPD::MimeType type,
        MPD::Representation* repr) {
  char buf[20];
  switch (type) {
    case MPD::MimeType::Video:
      {
        auto repr_ = dynamic_cast<MPD::VideoRepresentation*>(repr);
        switch (repr_->profile) {
          case MPD::ProfileLevel::Low:
            sprintf(buf, "avc1.42E0%02X", repr_->avc_level);
            break;
          case MPD::ProfileLevel::Main:
            sprintf(buf, "4D40%02X", repr_->avc_level);
            break;
          case MPD::ProfileLevel::High:
            sprintf(buf, "6400%02x", repr_->avc_level);
            break;
          default:
            throw std::runtime_error("Unsupported AVC profile");
        }
        break;
      }
    case MPD::MimeType::Audio:
      sprintf(buf, "mp4a.40.2");
      break;
    default:
      throw std::runtime_error("Unsupported MIME type");
  }
  return buf;
}


void MPDWriter::write_repr(MPD::Representation * repr)
{
  switch(repr->type) {
    case MPD::MimeType::Audio:
      {
        auto a = dynamic_cast<MPD::AudioRepresentation*>(repr);
        this->write_repr(a);
        break;
      }
    case MPD::MimeType::Video:
      {
        auto v = dynamic_cast<MPD::VideoRepresentation*>(repr);
        this->write_repr(v);
        break;
      }
    default:
      throw std::runtime_error("Unsupported MIME type");
  }
}

inline bool nearly_equal(float a, float b)
{
  return std::nextafter(a, std::numeric_limits<float>::lowest()) <= b
    && std::nextafter(a, std::numeric_limits<float>::max()) >= b;
}

void MPDWriter::write_framerate(float framerate)
{
    if(nearly_equal(framerate, 23.976))
        this->writer_->attr("frameRate", "24000/1001");
    else if(nearly_equal(framerate, 24))
        this->writer_->attr("frameRate", 24);
    else if(nearly_equal(framerate, 25))
        this->writer_->attr("frameRate", 25);
    else if(nearly_equal(framerate, 29.976))
        this->writer_->attr("frameRate", "30000/1001");
    else if(nearly_equal(framerate, 30))
        this->writer_->attr("frameRate", 30);
    else
        throw std::runtime_error("Unsupported frame rate");
}

/* THIS IS FOR VIDEO ONLY */
void MPDWriter::write_repr(MPD::VideoRepresentation * repr)
{
  this->writer_->open_elt("Representation");
  this->writer_->attr("id", "v" + repr->id);
  this->writer_->attr("mimeType", "video/mp4");
  std::string codec = this->write_codec(MPD::MimeType::Video, repr);
  this->writer_->attr("codecs", codec);
  this->writer_->attr("width", repr->width);
  this->writer_->attr("height", repr->height);
  this->write_framerate(repr->framerate);
  this->writer_->attr("startWithSAP", "1");
  this->writer_->attr("bandwidth", repr->bitrate);
  this->writer_->close_elt();
}

void MPDWriter::write_repr(MPD::AudioRepresentation * repr)
{
    this->writer_->open_elt("Representation");
    this->writer_->attr("id", "a" + repr->id);
    this->writer_->attr("mimeType", "audio/mp4");
    std::string codec = this->write_codec(MPD::MimeType::Audio, repr);
    this->writer_->attr("codecs", codec);
    this->writer_->attr("audioSamplingRate", repr->sampling_rate);
    this->writer_->attr("startWithSAP", "1");
    this->writer_->attr("bandwidth", repr->bitrate);
    this->writer_->close_elt();
}

void MPDWriter::write_adaption_set(MPD::AdaptionSet * set) {
  this->writer_->open_elt("AdaptationSet");
  /* ISO base main profile 8.5.2 */
  this->writer_->attr("segmentAlignment", "true");
  /* Audio channel */
  if(set->type == MPD::MimeType::Audio) {
  }

  /* write the segment template */
  this->writer_->open_elt("SegmentTemplate");
  this->writer_->attr("timescale", set->timescale);
  this->writer_->attr("duration", set->duration);
  this->writer_->attr("media", set->media_uri);
  /* the initial segment number */
  this->writer_->attr("startNumber", 1);
  this->writer_->attr("initialization", set->init_uri);
  this->writer_->close_elt();

  /* Write the segment */
  for(auto repr: set->get_repr_set())
    this->write_repr(repr);

  this->writer_->close_elt();
}

std::string MPDWriter::convert_pt(unsigned int seconds)
{
  /* This is only computes to 24 hours*/
  unsigned int hours = 0;
  unsigned int minutes = 0;
  if (seconds >= 3600) {
    hours = seconds / (3600);
    seconds %= 3600;
  }
  if(seconds >= 60) {
    minutes = seconds / 60;
    seconds %= 60;
  }
  // print out result
  std::string result = "PT";
  if(hours)
    result += std::to_string(hours) + "H";
  if(minutes)
    result += std::to_string(minutes) + "M";
  if(seconds)
    result += std::to_string(seconds) + "S";
  return result;
}

void MPDWriter::add_adaption_set(MPD::AdaptionSet *set)
{
  this->adaption_set_.insert(set);
}

std::string MPDWriter::flush()
{
  this->writer_->open_elt("MPD");
  /* MPD scheme */
  this->writer_->attr("xmlns:xsi",
      "http://www.w3.org/2001/XMLSchema-instance");
  this->writer_->attr("xmlns", "urn:mpeg:dash:schema:mpd:2011");
  this->writer_->attr("xmlns:xlink", "http://www.w3.org/1999/xlink");
  this->writer_->attr("xsi:schemaLocation",
      "urn:mpeg:DASH:schema:MPD:2011 \
http://standards.iso.org/ittf/PubliclyAvailableStandards/\
MPEG-DASH_schema_files/DASH-MPD.xsd");
  /* write the time when this MPD is flushed" */
  std::string t = this->format_time_now();
  this->writer_->attr("publishTime", t);
  this->writer_->attr("availabilityStartTime", t);
  this->writer_->attr("profiles", "urn:mpeg:dash:profile:isoff-live:2011");
  this->writer_->attr("type", "dynamic");
  this->writer_->attr("minBufferTime",
      this->convert_pt(this->min_buffer_time_));
  this->writer_->attr("minimumUpdatePeriod",
      this->convert_pt(this->update_period_));

  /* Base URL */
  this->writer_->open_elt("BaseURL");
  this->writer_->content(this->base_url_);
  this->writer_->close_elt();

  /* Period */
  /* only 1 period for now */
  this->writer_->open_elt("Period");
  this->writer_->attr("id", "1");
  /* Adaption Set */
  for (auto it: this->adaption_set_) {
    this->write_adaption_set(it);
  }

  /* Close all tags */
  this->writer_->close_all();

  return this->writer_->str();
}
