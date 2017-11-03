#ifndef TV_ENCODER_MPD_HH
#define TV_ENCODER_MPD_HH
#include <string>
#include <stack>
#include <sstream>
#include <memory>
#include <set>
#include <algorithm>
#include <iterator>
#include <chrono>

class XMLNode
{
public:
  const std::string tag_;
  bool hasContent;
  XMLNode(const std::string tag, bool hasContent): tag_(tag), hasContent(hasContent)
    { }
  XMLNode(const std::string tag): XMLNode(tag, false) { }
};

// based on
// https://gist.github.com/sebclaeys/1227644
// significant improvements are made
class XMLWriter
{
private:
  bool tag_open_;
  bool newline_;
  std::ostringstream os_;
  std::stack<XMLNode> elt_stack_;
  inline void close_tag();
  inline void indent();
  inline void write_escape(const std::string & str);

  const std::string xml_header = "<?xml version=\"1.0\" encoding=\"utf-8\"?>";
  const std::string xml_indent = "  ";

public:
  void open_elt(const std::string & tag);
  void close_elt();
  void close_all();

  void attr(const std::string & key, const std::string & val);
  void attr(const std::string & key, const unsigned int val);
  void attr(const std::string & key, const int val);

  void content(const int val);
  void content(const unsigned int val);
  void content(const std::string & val);

  std::string str();
  void output(std::ofstream &out);

  XMLWriter();
  ~XMLWriter();
};


namespace MPD {
enum class MimeType{ Video,  Audio_Webm, Audio_AAC };
const static uint8_t AvailableProfile[] {66, 88, 77, 100, 110, 122, 244, 44,
  83, 86, 128, 118, 138 };

struct Representation {
  std::string id;
  unsigned int bitrate;
  MimeType type;
  uint32_t timescale;
  uint32_t duration;

  Representation(std::string id, unsigned int bitrate, MimeType type,
      uint32_t timescale, uint32_t duration)
    : id(id), bitrate(bitrate), type(type), timescale(timescale),
      duration(duration)
  {}

  virtual ~Representation() {}
};

struct VideoRepresentation : public Representation {
  unsigned int width;
  unsigned int height;
  uint8_t profile;
  unsigned int avc_level;
  float framerate;

  VideoRepresentation(
      std::string id, unsigned int width, unsigned int height,
      unsigned int bitrate, uint8_t profile, unsigned int avc_level,
      float framerate, uint32_t timescale, uint32_t duration)
    : Representation(id, bitrate, MimeType::Video, timescale, duration),
      width(width), height(height), profile(profile),  avc_level(avc_level),
      framerate(framerate)
  {
    if (std::find(std::begin(AvailableProfile), std::end(AvailableProfile),
          profile) == std::end(AvailableProfile))
      throw std::runtime_error("Unsupported AVC profile.");
  }
};

struct AudioRepresentation : public Representation {
  unsigned int sampling_rate;

  AudioRepresentation(
      std::string id, unsigned int bitrate, unsigned int sampling_rate,
      bool use_opus, uint32_t timescale, uint32_t duration)
    : Representation(id, bitrate, use_opus? MimeType::Audio_Webm:
        MimeType::Audio_AAC, timescale, duration), sampling_rate(sampling_rate)
  {}
};

inline bool operator<(const Representation & a, const Representation & b)
{
  return a.id < b.id;
}

class AdaptionSet {
public:
  uint32_t id() const { return id_; }
  std::string init_uri() { return init_uri_; }
  std::string media_uri() { return media_uri_; }
  uint32_t duration() { return duration_; }
  virtual std::set<std::shared_ptr<Representation>> get_repr()
  { return std::set<std::shared_ptr<Representation>>(); }
  bool is_video() { return is_video_; }
  uint32_t get_start_number() { return start_number_; }

protected:
  void set_duration(uint32_t duration) { duration_ = duration; }

  AdaptionSet(int id, std::string init_uri, std::string media_uri,
      bool is_video, uint32_t start_number);

  virtual ~AdaptionSet() {}

private:
  uint32_t id_;
  std::string init_uri_;
  std::string media_uri_;
  uint32_t duration_;
  bool is_video_;
  uint32_t start_number_;
};

class AudioAdaptionSet : public AdaptionSet {
public:
  void add_repr(std::shared_ptr<AudioRepresentation> repr);

  AudioAdaptionSet(int id, std::string init_uri, std::string media_uri,
      uint32_t start_number = 1);

  std::set<std::shared_ptr<Representation>> get_repr();
  std::set<std::shared_ptr<AudioRepresentation>> get_audio_repr()
  { return repr_set_; }

private:
  std::set<std::shared_ptr<AudioRepresentation>> repr_set_;
};

class VideoAdaptionSet : public AdaptionSet {
public:
  float framerate() { return framerate_; }
  void set_framerate(float framerate) { framerate_ = framerate; }

  VideoAdaptionSet(int id, std::string init_uri, std::string media_uri,
      uint32_t start_number = 1);

  void add_repr(std::shared_ptr<VideoRepresentation> repr);

  std::set<std::shared_ptr<Representation>> get_repr();
  std::set<std::shared_ptr<VideoRepresentation>> get_video_repr()
  { return repr_set_; }

private:
  float framerate_ = 0; /* this will be set later */
  std::set<std::shared_ptr<VideoRepresentation>> repr_set_;
};

inline bool operator<(const AdaptionSet & a, const AdaptionSet & b)
{
  return a.id() < b.id();
}
}

class MPDWriter
{
public:
  MPDWriter(uint32_t medua_duration, uint32_t min_buffer_time,
      std::string base_url);
  ~MPDWriter();
  std::string flush();
  void add_video_adaption_set(std::shared_ptr<MPD::VideoAdaptionSet> set);
  void add_audio_adaption_set(std::shared_ptr<MPD::AudioAdaptionSet> set);
  void set_publish_time(const std::chrono::seconds time)
  { publish_time_ = time; }
  void set_video_start_number(uint32_t number)
  { v_start_number_ = number; }
  void set_audio_start_number(uint32_t number)
  { a_start_number_ = number; }
  void set_presentation_time_offset(uint32_t offset)
  { presentation_time_offset_ = offset; }

private:
  uint32_t media_duration_;
  uint32_t min_buffer_time_;
  std::chrono::seconds publish_time_;
  std::unique_ptr<XMLWriter> writer_;
  std::string base_url_;
  uint32_t v_start_number_ = 0;
  uint32_t a_start_number_ = 0;
  uint32_t presentation_time_offset_ = 0;

  std::set<std::shared_ptr<MPD::VideoAdaptionSet>> video_adaption_set_;
  std::set<std::shared_ptr<MPD::AudioAdaptionSet>> audio_adaption_set_;
  std::string format_time(const time_t time);
  std::string format_time(const std::chrono::seconds & time);
  std::string format_time_now();
  void write_adaption_set(std::shared_ptr<MPD::AdaptionSet>set);
  void write_video_adaption_set(std::shared_ptr<MPD::VideoAdaptionSet> set);
  void write_audio_adaption_set(std::shared_ptr<MPD::AudioAdaptionSet> set);
  std::string write_video_codec(std::shared_ptr<MPD::VideoRepresentation> repr);
  std::string write_audio_codec(std::shared_ptr<MPD::AudioRepresentation> repr);
  void write_repr(std::shared_ptr<MPD::Representation> repr);
  void write_video_repr(std::shared_ptr<MPD::VideoRepresentation> repr);
  void write_audio_repr(std::shared_ptr<MPD::AudioRepresentation> repr);
  std::string convert_pt(unsigned int seconds);
  void write_framerate(const float & framerate);
};

#endif /* TV_ENCODER_MPD_HH */
