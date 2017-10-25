#ifndef TV_ENCODER_MPD_HH
#define TV_ENCODER_MPD_HH
#include <string>
#include <stack>
#include <sstream>
#include <memory>
#include <set>

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
  XMLWriter& open_elt(const std::string & tag);
  XMLWriter& close_elt();
  XMLWriter& close_all();

  XMLWriter& attr(const std::string & key, const std::string & val);
  XMLWriter& attr(const std::string & key, const unsigned int & val);
  XMLWriter& attr(const std::string & key, const int & val);

  XMLWriter& content(const int & val);
  XMLWriter& content(const unsigned int & val);
  XMLWriter& content(const std::string & val);

  std::string str();
  void output(std::ofstream &out);

  XMLWriter();
  ~XMLWriter();
};


namespace MPD {
enum class MimeType{ Video,  Audio_Webm, Audio_AAC };

enum class ProfileLevel { Low, Main, High };

struct Representation {
  std::string id;
  unsigned int bitrate;
  MimeType type;

  Representation(std::string id, unsigned int bitrate, MimeType type):
    id(id), bitrate(bitrate), type(type)
  {}

  virtual ~Representation() {}
};

struct VideoRepresentation: public Representation {
  unsigned int width;
  unsigned int height;
  ProfileLevel profile;
  unsigned int avc_level;
  float framerate;

  VideoRepresentation(std::string id, unsigned int width, unsigned int height,
      unsigned int bitrate, ProfileLevel profile, unsigned int avc_level,
      float framerate): Representation(id, bitrate, MimeType::Video),
  width(width), height(height), profile(profile),  avc_level(avc_level),
  framerate(framerate)
  {}
};

struct AudioRepresentation: public Representation {
  unsigned int sampling_rate;

  AudioRepresentation(std::string id, unsigned int bitrate,
          unsigned int sampling_rate, bool use_opus): Representation(id,
              bitrate, use_opus? MimeType::Audio_Webm: MimeType::Audio_AAC),
          sampling_rate(sampling_rate)
  {}
};

inline bool operator<(const Representation & a, const Representation & b)
{
  return a.id < b.id;
}

class AdaptionSet {
public:
  int id;
  std::string init_uri;
  std::string media_uri;
  unsigned int duration; /* This needs to be determined from mp4 info */
  unsigned int timescale; /* this as well */

  AdaptionSet(int id, std::string init_uri, std::string media_uri,
      unsigned int duration, unsigned int timescale);

  virtual ~AdaptionSet() {}
 
};

class AudioAdaptionSet : public AdaptionSet {
public:
  void add_repr(const AudioRepresentation && repr);

  AudioAdaptionSet(int id, std::string init_uri, std::string media_uri,
    unsigned int duration, unsigned int timescale);
  
  std::set<AudioRepresentation> get_repr_set() const
  { return repr_set_; }

private:
  std::set<AudioRepresentation> repr_set_;
};

class VideoAdaptionSet : public AdaptionSet {
public:
  float framerate;

  VideoAdaptionSet(int id, std::string init_uri, std::string media_uri,
      float framerate, unsigned int duration, unsigned int timescale);

  void add_repr(const VideoRepresentation && repr);
  
  std::set<VideoRepresentation> get_repr_set() const 
  { return repr_set_; }

private:
  std::set<VideoRepresentation> repr_set_;
};

inline bool operator<(const AdaptionSet & a, const AdaptionSet & b)
{
  return a.id < b.id;
}
}

class MPDWriter
{
public:
  MPDWriter(int64_t update_period, int64_t min_buffer_time,
          std::string base_url);
  ~MPDWriter();
  std::string flush();
  void add_video_adaption_set(const MPD::VideoAdaptionSet && set);
  void add_audio_adaption_set(const MPD::AudioAdaptionSet && set);

private:
  int64_t update_period_;
  int64_t min_buffer_time_;
  std::unique_ptr<XMLWriter> writer_;
  std::string base_url_;
  std::set<MPD::VideoAdaptionSet> video_adaption_set_;
  std::set<MPD::AudioAdaptionSet> audio_adaption_set_;
  std::string format_time_now();
  void write_adaption_set(const MPD::AdaptionSet & set);
  void write_video_adaption_set(const MPD::VideoAdaptionSet & set);
  void write_audio_adaption_set(const MPD::AudioAdaptionSet & set);
  std::string write_video_codec(const MPD::VideoRepresentation & repr);
  std::string write_audio_codec(const MPD::AudioRepresentation & repr);
  void write_repr(const MPD::Representation & repr);
  void write_video_repr(const MPD::VideoRepresentation & repr);
  void write_audio_repr(const MPD::AudioRepresentation & repr);
  std::string convert_pt(unsigned int seconds);
  void write_framerate(const float & framerate);
};

#endif /* TV_ENCODER_MPD_HH */
