#include "mpd.hh"
#include <sstream>
#include <stack>
#include <set>
#include <iostream>
#include <fstream>
#include <memory>
#include <chrono>
#include <string>
#include <limits>
#include <cmath>
#include <time.h>

XMLWriter::XMLWriter(): tag_open_(false), newline_( true),
  os_(std::ostringstream()), elt_stack_(std::stack<XMLNode>())
{
  os_ << (xml_header) << std::endl;
}

XMLWriter::~XMLWriter()
{}

void XMLWriter::open_elt(const std::string & tag)
{
  close_tag();
  if (elt_stack_.size() > 0) {
    os_ << std::endl;
    indent();
    elt_stack_.top().hasContent = true;
  }
  os_ << "<" << tag;
  elt_stack_.push(XMLNode(tag));
  tag_open_ = true;
  newline_ = false;
}

void XMLWriter::close_elt()
{
  if (!elt_stack_.size())
    throw std::runtime_error("XMLWriter is in an incorrect state.");
  XMLNode node = elt_stack_.top();
  elt_stack_.pop();
  if (!node.hasContent) {
    /* no actual value, maybe just attr */
    os_ << " />";
    tag_open_ = false;
  } else {
    close_tag();
    if (newline_) {
      os_ << std::endl;
      indent();
    }
    os_ << "</" << node.tag_ << ">";
  }
  newline_ = true;
}

void XMLWriter::close_all()
{
  while (elt_stack_.size())
    close_elt();
}

void XMLWriter::attr(const std::string & key, const std::string & val)
{
  os_ << " " << key << "=\"";
  write_escape(val);
  os_ << "\"";
}

void XMLWriter::attr(const std::string & key, const unsigned int val)
{
  attr(key, std::to_string(val));
}

void XMLWriter::attr(const std::string & key, const int val)
{
  attr(key, std::to_string(val));
}

void XMLWriter::content(const int val)
{
  content(std::to_string(val));
}

void XMLWriter::content(const unsigned int val)
{
  content(std::to_string(val));
}

void XMLWriter::content(const std::string & val)
{
  close_tag();
  write_escape(val.c_str());
  elt_stack_.top().hasContent = true;
}

inline void XMLWriter::close_tag()
{
  if (tag_open_) {
     os_ << ">";
    tag_open_ = false;
  }
}

inline void XMLWriter::indent()
{
  for (unsigned int i = 0; i < elt_stack_.size(); i++)
    os_ << xml_indent;
}

inline void XMLWriter::write_escape(const std::string & str)
{
  for (unsigned int i = 0; i < str.length();)
    switch (str[i]) {
      case '&': os_ << "&amp;"; break;
      case '<': os_ << "&lt;"; break;
      case '>': os_ << "&gt;"; break;
      case '\'': os_ << "&apos;"; break;
      case '"': os_ << "&quot;"; break;
      default: os_.put(str[i]); i++; break;
    }
}

std::string XMLWriter::str()
{
  return os_.str();
}

void XMLWriter::output(std::ofstream &out)
{
  out << str();
}

MPD::AdaptionSet::AdaptionSet(int id, std::string init_uri,
        std::string media_uri,
    unsigned int duration, unsigned int timescale):
  id(id), init_uri(init_uri), media_uri(media_uri),
    duration(duration), timescale(timescale)
{}

MPD::VideoAdaptionSet::VideoAdaptionSet(int id, std::string init_uri,
        std::string media_uri, float framerate, unsigned int duration,
        unsigned int timescale): AdaptionSet(id, init_uri, media_uri,
            duration, timescale), framerate(framerate), repr_set_()
{}

MPD::AudioAdaptionSet::AudioAdaptionSet(int id, std::string init_uri,
    std::string media_uri, unsigned int duration, unsigned int
    timescale) : AdaptionSet(id, init_uri, media_uri, duration,
      timescale), repr_set_()
{}

void MPD::AudioAdaptionSet::add_repr(std::shared_ptr<AudioRepresentation> repr)
{
  repr_set_.insert(repr);
}

void MPD::VideoAdaptionSet::add_repr(std::shared_ptr<VideoRepresentation> repr)
{
  repr_set_.insert(repr);
}

MPDWriter::MPDWriter(int64_t update_period, int64_t min_buffer_time,
    std::string base_url):
  update_period_(update_period), min_buffer_time_(min_buffer_time),
  availability_start_time_(std::time(NULL)),
  writer_(std::make_unique<XMLWriter>()), base_url_(base_url),
  video_adaption_set_(), audio_adaption_set_()
{}

MPDWriter::~MPDWriter()
{}

std::string MPDWriter::format_time(const time_t time)
{
    char buf[42];
    tm * now_tm= gmtime(&time);
    strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", now_tm);
    return buf; 
}

std::string MPDWriter::format_time(const std::chrono::seconds & time)
{
  time_t t = static_cast<time_t>(time.count());
  return format_time(t);
}

std::string MPDWriter::format_time_now()
{
  /* this is possible because C++ will convert char* into std:string
   * therefore no pointer to stack problem                        */
  time_t now= time(0);
  return format_time(now);
}


std::string MPDWriter::write_video_codec(
        std::shared_ptr<MPD::VideoRepresentation> repr)
{
  char buf[20];
  sprintf(buf, "avc1.%02XE0%02X", repr->profile, repr->avc_level);
  return buf;
}

std::string MPDWriter::write_audio_codec(
        std::shared_ptr<MPD::AudioRepresentation> repr)
{
  char buf[20];
  if(repr->type == MPD::MimeType::Audio_AAC)
    sprintf(buf, "mp4a.40.2");
  else if( repr->type == MPD::MimeType::Audio_Webm)
    sprintf(buf, "opus"); /* assume it is opus format */
  else
    throw std::runtime_error("Unsupported MIME type");
  return buf;
}


inline bool nearly_equal(const float & a, const float & b)
{
  return std::nextafter(a, std::numeric_limits<float>::lowest()) <= b
    and std::nextafter(a, std::numeric_limits<float>::max()) >= b;
}

void MPDWriter::write_framerate(const float & framerate)
{
    if(nearly_equal(framerate, 23.976))
      writer_->attr("frameRate", "24000/1001");
    else if(nearly_equal(framerate, 24))
      writer_->attr("frameRate", 24);
    else if(nearly_equal(framerate, 25))
      writer_->attr("frameRate", 25);
    else if(nearly_equal(framerate, 29.976))
      writer_->attr("frameRate", "30000/1001");
    else if(nearly_equal(framerate, 30))
      writer_->attr("frameRate", 30);
    else if(nearly_equal(framerate, 59.94))
      writer_->attr("frameRate", "600000/1001");
    else if(nearly_equal(framerate, 60))
      writer_->attr("frameRate", 60);
    else
      throw std::runtime_error("Unsupported frame rate");
}

/* THIS IS FOR VIDEO ONLY */
void MPDWriter::write_video_repr(std::shared_ptr<MPD::VideoRepresentation> repr)
{
  writer_->open_elt("Representation");
  writer_->attr("id", "v" + repr->id);
  writer_->attr("mimeType", "video/mp4");
  std::string codec = write_video_codec(repr);
  writer_->attr("codecs", codec);
  writer_->attr("width", repr->width);
  writer_->attr("height", repr->height);
  write_framerate(repr->framerate);
  writer_->attr("startWithSAP", "1");
  writer_->attr("bandwidth", repr->bitrate);
  writer_->close_elt();
}

void MPDWriter::write_audio_repr(std::shared_ptr<MPD::AudioRepresentation> repr)
{
    writer_->open_elt("Representation");
    writer_->attr("id", "a" + repr->id);
    writer_->attr("mimeType", repr->type == MPD::MimeType::Audio_AAC?
        "audio/mp4": "audio/webm");
    std::string codec = write_audio_codec(repr);
    writer_->attr("codecs", codec);
    writer_->attr("audioSamplingRate", repr->sampling_rate);
    writer_->attr("startWithSAP", "1");
    writer_->attr("bandwidth", repr->bitrate);
    writer_->close_elt();
}

void MPDWriter::write_video_adaption_set(
        std::shared_ptr<MPD::VideoAdaptionSet> set)
{
  writer_->open_elt("AdaptationSet");
  write_adaption_set(set);

  /* Write the segment */
  for(auto repr : set->get_repr_set())
    write_video_repr(repr);

  writer_->close_elt();
}

void MPDWriter::write_audio_adaption_set(
        std::shared_ptr<MPD::AudioAdaptionSet> set)
{
  writer_->open_elt("AdaptationSet");
  write_adaption_set(set);

  /* Write the segment */
  for(auto repr : set->get_repr_set())
    write_audio_repr(repr);

  writer_->close_elt();
}

void MPDWriter::write_adaption_set(std::shared_ptr<MPD::AdaptionSet> set) {
  /* ISO base main profile 8.5.2 */
  writer_->attr("segmentAlignment", "true");

  /* write the segment template */
  writer_->open_elt("SegmentTemplate");
  writer_->attr("timescale", set->timescale);
  writer_->attr("duration", set->duration);
  writer_->attr("media", set->media_uri);
  /* the initial segment number */
  writer_->attr("startNumber", 1);
  writer_->attr("initialization", set->init_uri);
  writer_->close_elt();
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

void MPDWriter::add_video_adaption_set(
        std::shared_ptr<MPD::VideoAdaptionSet> set)
{
  video_adaption_set_.insert(set);
}


void MPDWriter::add_audio_adaption_set(
        std::shared_ptr<MPD::AudioAdaptionSet> set)
{
  audio_adaption_set_.insert(set);
}

std::string MPDWriter::flush()
{
  writer_->open_elt("MPD");
  /* MPD scheme */
  writer_->attr("xmlns:xsi",
      "http://www.w3.org/2001/XMLSchema-instance");
  writer_->attr("xmlns", "urn:mpeg:dash:schema:mpd:2011");
  writer_->attr("xmlns:xlink", "http://www.w3.org/1999/xlink");
  writer_->attr("xsi:schemaLocation",
      "urn:mpeg:DASH:schema:MPD:2011 \
http://standards.iso.org/ittf/PubliclyAvailableStandards/\
MPEG-DASH_schema_files/DASH-MPD.xsd");
  /* write the time when this MPD is flushed" */
  std::string t = format_time_now();
  writer_->attr("publishTime", t);
  writer_->attr("availabilityStartTime", t);
  writer_->attr("profiles", "urn:mpeg:dash:profile:isoff-live:2011");
  writer_->attr("type", "dynamic");
  writer_->attr("minBufferTime", convert_pt(min_buffer_time_));
  writer_->attr("minimumUpdatePeriod", convert_pt(update_period_));

  /* Base URL */
  writer_->open_elt("BaseURL");
  writer_->content(base_url_);
  writer_->close_elt();

  /* Period */
  /* only 1 period for now */
  writer_->open_elt("Period");
  writer_->attr("id", "1");
  /* Adaption Set */
  for (auto it: video_adaption_set_) {
    write_video_adaption_set(it);
  }

  for (auto it: audio_adaption_set_) {
    write_audio_adaption_set(it);
  }

  /* Close all tags */
  writer_->close_all();

  return writer_->str();
}
