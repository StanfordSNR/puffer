#include "mpd.hh"

#include <time.h>
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

using std::string;
using std::runtime_error;
using std::endl;
using std::shared_ptr;

XMLWriter::XMLWriter()
  : tag_open_(false), newline_(true), os_(std::ostringstream()),
    elt_stack_(std::stack<XMLNode>())
{
  os_ << (xml_header) << endl;
}

XMLWriter::~XMLWriter()
{}

void XMLWriter::open_elt(const string & tag)
{
  close_tag();
  if (elt_stack_.size() > 0) {
    os_ << endl;
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
  if (not elt_stack_.size()) {
    throw std::runtime_error("XMLWriter is in an incorrect state.");
  }
  XMLNode node = elt_stack_.top();
  elt_stack_.pop();
  if (not node.hasContent) {
    /* no actual value, maybe just attr */
    os_ << " />";
    tag_open_ = false;
  } else {
    close_tag();
    if (newline_) {
      os_ << endl;
      indent();
    }
    os_ << "</" << node.tag_ << ">";
  }
  newline_ = true;
}

void XMLWriter::close_all()
{
  while (elt_stack_.size()) {
    close_elt();
  }
}

void XMLWriter::attr(const string & key, const string & val)
{
  os_ << " " << key << "=\"";
  write_escape(val);
  os_ << "\"";
}

void XMLWriter::attr(const string & key, const unsigned int val)
{
  attr(key, std::to_string(val));
}

void XMLWriter::attr(const string & key, const int val)
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

void XMLWriter::content(const string & val)
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
  for (unsigned int i = 0; i < elt_stack_.size(); i++) {
    os_ << xml_indent;
  }
}

inline void XMLWriter::write_escape(const string & str)
{
  for (unsigned int i = 0; i < str.length();) {
    switch (str[i]) {
      case '&': os_ << "&amp;"; break;
      case '<': os_ << "&lt;"; break;
      case '>': os_ << "&gt;"; break;
      case '\'': os_ << "&apos;"; break;
      case '"': os_ << "&quot;"; break;
      default: os_.put(str[i]); i++; break;
    }
  }
}

string XMLWriter::str()
{
  return os_.str();
}

void XMLWriter::output(std::ofstream &out)
{
  out << str();
}

MPD::AdaptionSet::AdaptionSet(
    int id, string init_uri, string media_uri)
  : id_(id), init_uri_(init_uri), media_uri_(media_uri), duration_(0)
{}

MPD::VideoAdaptionSet::VideoAdaptionSet(
    int id, string init_uri, string media_uri)
  : AdaptionSet(id, init_uri, media_uri), repr_set_()
{}

MPD::AudioAdaptionSet::AudioAdaptionSet(
    int id, string init_uri, string media_uri)
  : AdaptionSet(id, init_uri, media_uri), repr_set_()
{}

std::set<shared_ptr<MPD::Representation>>
MPD::AudioAdaptionSet::get_repr()
{
  std::set<shared_ptr<MPD::Representation>> set;
  for (const auto & repr : repr_set_) {
    set.insert(repr);
  }
  return set;
}

std::set<shared_ptr<MPD::Representation>>
MPD::VideoAdaptionSet::get_repr()
{
  std::set<shared_ptr<MPD::Representation>> set;
  for (const auto & repr : repr_set_) {
    set.insert(repr);
  }
  return set;
}

/*
 * C++ doesn't allow shared_ptr as a non-type parameter for a template
 */
void MPD::AdaptionSet::check_data(shared_ptr<Representation> repr)
{
  if (duration() == 0) {
    /* fisrt repr */
    set_duration(repr->duration);
  } else if (duration() != repr->duration) {
    /* duration mismatch */
    throw runtime_error("representation duration does not match with "
                        "the adaption set");
  }

  if (timescale() == 0) {
     set_timescale(repr->timescale);
  } else if (timescale() != repr->timescale) {
    /* timescale mismatch */
    throw runtime_error("representation timescale does not math with the "
                        "adaption set");
  }
}

void MPD::AudioAdaptionSet::add_repr(shared_ptr<AudioRepresentation> repr)
{
  AdaptionSet::check_data(repr);
  repr_set_.insert(repr);
}

void MPD::VideoAdaptionSet::add_repr(shared_ptr<VideoRepresentation> repr)
{
  AdaptionSet::check_data(repr);
  repr_set_.insert(repr);
}

MPDWriter::MPDWriter(uint32_t min_buffer_time,
                     string base_url,
                     string time_url)
  : min_buffer_time_(min_buffer_time), publish_time_(std::time(nullptr)),
    writer_(std::make_unique<XMLWriter>()), base_url_(base_url),
    time_url_(time_url), video_adaption_set_(), audio_adaption_set_()
{}

MPDWriter::~MPDWriter()
{}

string MPDWriter::format_time(const time_t time)
{
  char buf[42];
  tm now_tm;
  gmtime_r(&time, &now_tm);
  strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &now_tm);
  return buf;
}

string MPDWriter::format_time(const std::chrono::seconds & time)
{
  time_t t = static_cast<time_t>(time.count());
  return format_time(t);
}

string MPDWriter::format_time_now()
{
  /* this is possible because C++ will convert char* into std:string
   * therefore no pointer to stack problem                        */
  time_t now = time(0);
  return format_time(now);
}


string MPDWriter::write_video_codec(shared_ptr<MPD::VideoRepresentation> repr)
{
  char buf[20];
  snprintf(buf, sizeof(buf), "avc1.%02XE0%02X", repr->profile,
      repr->avc_level);
  return buf;
}

string MPDWriter::write_audio_codec(shared_ptr<MPD::AudioRepresentation> repr)
{
  if (repr->type == MPD::MimeType::Audio_AAC_LC) {
    return "mp4a.40.2";
  } else if (repr->type == MPD::MimeType::Audio_HE_AAC) {
    return "mp4a.40.5";
  } else if (repr->type == MPD::MimeType::Audio_MP3) {
    return "mp4a.40.34";
  } else if (repr->type == MPD::MimeType::Audio_OPUS) {
    return "opus"; /* assume it is opus format */
  } else {
    throw std::runtime_error("Unsupported MIME type");
  }
}


inline bool nearly_equal(const float & a, const float & b)
{
  return std::abs(a - b) < 0.01;
}

void MPDWriter::write_framerate(const float & framerate)
{
  if (nearly_equal(framerate, 23.976)) {
    writer_->attr("frameRate", "24000/1001");
  } else if (nearly_equal(framerate, 24)) {
    writer_->attr("frameRate", 24);
  } else if (nearly_equal(framerate, 25)) {
    writer_->attr("frameRate", 25);
  } else if (nearly_equal(framerate, 29.976)) {
    writer_->attr("frameRate", "30000/1001");
  } else if (nearly_equal(framerate, 30)) {
    writer_->attr("frameRate", 30);
  } else if (nearly_equal(framerate, 59.94)) {
    writer_->attr("frameRate", "60000/1001");
  } else if (nearly_equal(framerate, 60)) {
    writer_->attr("frameRate", 60);
  } else {
    throw std::runtime_error("Unsupported frame rate " +
                             std::to_string(framerate));
  }
}

/* THIS IS FOR VIDEO ONLY */
void MPDWriter::write_video_repr(shared_ptr<MPD::VideoRepresentation> repr)
{
  writer_->open_elt("Representation");
  writer_->attr("id", repr->id);
  writer_->attr("mimeType", "video/mp4");
  string codec = write_video_codec(repr);
  writer_->attr("codecs", codec);
  writer_->attr("width", repr->width);
  writer_->attr("height", repr->height);
  write_framerate(repr->framerate);
  writer_->attr("startWithSAP", "1");
  writer_->attr("bandwidth", repr->bitrate);
  writer_->close_elt();
}

void MPDWriter::write_audio_repr(shared_ptr<MPD::AudioRepresentation> repr)
{
  writer_->open_elt("Representation");
  writer_->attr("id", repr->id);
  writer_->attr("mimeType", repr->type != MPD::MimeType::Audio_OPUS?
                "audio/mp4": "audio/webm");
  string codec = write_audio_codec(repr);
  writer_->attr("codecs", codec);
  writer_->attr("audioSamplingRate", repr->sampling_rate);
  writer_->attr("startWithSAP", "1");
  writer_->attr("bandwidth", repr->bitrate);
  writer_->close_elt();
}

void MPDWriter::write_video_adaption_set(shared_ptr<MPD::VideoAdaptionSet> set)
{
  writer_->open_elt("AdaptationSet");
  write_adaption_set(set);

  /* Write the segment */
  for (const auto & repr : set->get_video_repr()) {
    write_video_repr(repr);
  }

  writer_->close_elt();
}

void MPDWriter::write_audio_adaption_set(shared_ptr<MPD::AudioAdaptionSet> set)
{
  writer_->open_elt("AdaptationSet");
  write_adaption_set(set);

  /* Write the segment */
  for (const auto & repr : set->get_audio_repr()) {
    write_audio_repr(repr);
  }

  writer_->close_elt();
}

void MPDWriter::write_adaption_set(shared_ptr<MPD::AdaptionSet> set)
{
  /* get timescale from its representation sets
  * notice that by stanford all representation should have the same
  * timescale */
  uint32_t timescale = 0;
  for (const auto & repr : set->get_repr()) {
    if (timescale == 0) {
      timescale = repr->timescale;
    } else {
      if (timescale != repr->timescale) {
        throw std::runtime_error("Inconsistent timescale in adaption set.");
      }
    }
  }

  /* ISO base main profile 8.5.2 */
  writer_->attr("segmentAlignment", "true");

  /* write the segment template */
  writer_->open_elt("SegmentTemplate");
  writer_->attr("media", set->media_uri());
  writer_->attr("timescale", timescale);
  /* the initial segment number */
  writer_->attr("initialization", set->init_uri());
  /* offset the audio segment */
  if (set->presentation_time_offset() != 0) {
    writer_->attr("presentationTimeOffset", set->presentation_time_offset());
  }

  writer_->attr("duration", set->duration());

  /* remove timeline to make it cleaner */
  /* write dummy segment timeline */
  /* writer_->open_elt("SegmentTimeline");
  writer_->open_elt("S");
  writer_->attr("d", set->duration());
  // use the largest number because each segment will be the same length
  writer_->attr("r", 0xFFFFFFFF);
  writer_->close_elt();
  writer_->close_elt();
  */

  /* allow bitstream switching */
  writer_->attr("bitstreamSwitching", "true");

  writer_->close_elt();
}

string MPDWriter::convert_pt(unsigned int seconds)
{
  unsigned int hours = 0;
  unsigned int minutes = 0;
  if (seconds >= 3600) {
    hours = seconds / (3600);
    seconds %= 3600;
  }
  if (seconds >= 60) {
    minutes = seconds / 60;
    seconds %= 60;
  }
  // print out result
  string result = "PT";
  if (hours) {
    result += std::to_string(hours) + "H";
  }
  if (minutes) {
    result += std::to_string(minutes) + "M";
  }
  if (seconds) {
    result += std::to_string(seconds) + "S";
  }
  return result;
}

void MPDWriter::add_video_adaption_set(shared_ptr<MPD::VideoAdaptionSet> set)
{
  video_adaption_set_.insert(set);
}


void MPDWriter::add_audio_adaption_set(shared_ptr<MPD::AudioAdaptionSet> set)
{
  audio_adaption_set_.insert(set);
}

string MPDWriter::flush()
{
  writer_->open_elt("MPD");
  /* MPD scheme */
  writer_->attr("xmlns:xsi",
                "http://www.w3.org/2001/XMLSchema-instance");
  writer_->attr("xmlns", "urn:mpeg:dash:schema:mpd:2011");
  writer_->attr("xmlns:xlink", "http://www.w3.org/1999/xlink");
  writer_->attr("xsi:schemaLocation",
                "urn:mpeg:DASH:schema:MPD:2011 "
                "http://standards.iso.org/ittf/PubliclyAvailableStandards/"
                "MPEG-DASH_schema_files/DASH-MPD.xsd");
  writer_->attr("publishTime", format_time(publish_time_));
  writer_->attr("profiles", "urn:mpeg:dash:profile:isoff-live:2011");
  writer_->attr("type", mpd_type);
  writer_->attr("minBufferTime", convert_pt(min_buffer_time_));
  /* set to epoch 0 */
  writer_->attr("availabilityStartTime", format_time(0));
  /* set to 60 seconds as it really doesn't matter:
   * the mpd file will not change over time
   */
  writer_->attr("minimumUpdatePeriod", convert_pt(60));

  /* Base URL */
  if (not base_url_.empty()) {
    writer_->open_elt("BaseURL");
    writer_->content(base_url_);
    writer_->close_elt();
  }

  /* Period */
  /* only 1 period for now */
  writer_->open_elt("Period");
  writer_->attr("id", "1");
  /* start right away */
  writer_->attr("start", "PT0S");
  /* Adaption Set */
  for (const auto & it : video_adaption_set_) {
    write_video_adaption_set(it);
  }

  for (const auto & it : audio_adaption_set_) {
    write_audio_adaption_set(it);
  }

  /* Period end */
  writer_->close_elt();

  /* write UTCTiming tag */
  writer_->open_elt("UTCTiming");
  writer_->attr("schemeIdUri", utctiming_scheme);
  writer_->attr("value", time_url_);

  /* Close all tags */
  writer_->close_all();

  return writer_->str();
}
