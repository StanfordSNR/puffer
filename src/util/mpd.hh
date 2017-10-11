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
  const char *tag_;
  bool hasContent;
  XMLNode(const char *tag, bool hasContent): tag_(tag), hasContent(hasContent) { }
  XMLNode(const char *tag): XMLNode(tag, false) { }
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
  inline void write_escape(const char* str);

public:
  XMLWriter& open_elt(const char* tag);
  XMLWriter& close_elt();
  XMLWriter& close_all();

  XMLWriter& attr(const char* key, const char* val);
  XMLWriter& attr(const char* key, std::string val);
  XMLWriter& attr(const char* key, unsigned int val);
  XMLWriter& attr(const char* key, int val);

  XMLWriter& content(const char* val);
  XMLWriter& content(const int val);
  XMLWriter& content(const unsigned int val);
  XMLWriter& content(const std::string & val);

  std::string str();
  void output(std::ofstream &out);

  XMLWriter();
  ~XMLWriter();
};


namespace MPD {
enum class MimeType{ Video,  Audio };

enum class ProfileLevel { Low, Main, High };

struct Representation {
  std::string id_;
  unsigned int bitrate;
  MimeType type_;

  Representation(std::string id, unsigned int bitrate, MimeType type):
    id_(id), bitrate(bitrate), type_(type)
  {}

  virtual ~Representation() {}
};

struct VideoRepresentation: public Representation {
  unsigned int width;
  unsigned int height;
  ProfileLevel profile;
  unsigned int avc_level;
  unsigned int framerate_;

  VideoRepresentation(std::string id, unsigned int width, unsigned int height,
          unsigned int bitrate, ProfileLevel profile, unsigned int avc_level,
          unsigned int framerate): Representation(id, bitrate, MimeType::Video), 
  width(width), height(height), profile(profile),  avc_level(avc_level), 
  framerate_(framerate)
  {}
};

struct AudioRepresentation: public Representation {
  unsigned int sampling_rate_;

  AudioRepresentation(std::string id, unsigned int bitrate, unsigned int sampling_rate)
      : Representation(id, bitrate, MimeType::Audio), sampling_rate_(sampling_rate)
  {}
};

inline bool operator<(const Representation & a, const Representation & b)
{
  return a.id_ < b.id_;
}

class AdaptionSet {
public:
  int id_;
  std::string init_uri_;
  std::string media_uri_;
  unsigned int duration_; /* This needs to be determined from mp4 info */
  unsigned int timescale_; /* this as well */
  MimeType type;

  AdaptionSet(int id, std::string init_uri, std::string media_uri,
      unsigned int duration, unsigned int timescale, MimeType type);

  virtual ~AdaptionSet() {} 

  std::set<Representation*> repr_set_;

};

class AudioAdaptionSet : public AdaptionSet {
public:
  void add_repr(AudioRepresentation * repr);

  AudioAdaptionSet(int id, std::string init_uri, std::string media_uri,
    unsigned int duration, unsigned int timescale);

};

class VideoAdaptionSet : public AdaptionSet {
public:
  unsigned int framerate_;

  VideoAdaptionSet(int id, std::string init_uri, std::string media_uri,
      unsigned int framerate, unsigned int duration, unsigned int timescale);

  void add_repr(VideoRepresentation * repr);
};

inline bool operator<(const AdaptionSet & a, const AdaptionSet & b)
{
  return a.id_ < b.id_;
}
}

class MPDWriter
{
public:
  MPDWriter(int64_t update_period, std::string base_url);
  ~MPDWriter();
  std::string flush();
  void add_adaption_set(MPD::AdaptionSet * set);
private:
  int64_t update_period_;
  std::unique_ptr<XMLWriter> writer_;
  std::string base_url_;
  std::set<MPD::AdaptionSet*> adaption_set_;
  std::string format_time_now();
  void write_adaption_set(MPD::AdaptionSet * set);
  std::string write_codec(MPD::MimeType type, MPD::Representation * repr);
  void write_repr(MPD::Representation * repr);
  void write_repr(MPD::VideoRepresentation * repr);
  void write_repr(MPD::AudioRepresentation * repr);
  std::string convert_pt(unsigned int seconds);
};

#endif /* TV_ENCODER_MPD_HH */
