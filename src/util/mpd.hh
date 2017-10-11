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

/* For videos now */
struct Representation {
  std::string id_;
  unsigned int width;
  unsigned int height;
  unsigned int bitrate;
  ProfileLevel profile;
  unsigned int avc_level;
  MimeType type_;
  unsigned int framerate_;
};

inline bool operator<(const Representation & a, const Representation & b)
{
  return a.id_ < b.id_;
}

class AdaptionSet{
public:
  std::string init_uri_;
  std::string media_uri_;
  unsigned int framerate_;
  unsigned int duration_; /* in seconds */

  std::set<Representation> repr_set_;

  AdaptionSet(std::string init_uri, std::string media_uri,
      unsigned int framerate, unsigned int duration);

  ~AdaptionSet();
  void add_repr(std::string id, unsigned int width, unsigned int height, 
      unsigned int bitrate, unsigned int avc_level, MPD::MimeType type,
      unsigned int framerate_);
};
}

class MPDWriter
{
public:
  MPDWriter(int64_t update_period, std::string string, std::string base_url);
  ~MPDWriter();
  std::string flush();
  void add_adaption_set(MPD::MimeType type, int framerate);
private:
  int64_t update_period_;
  std::string title_;
  std::unique_ptr<XMLWriter> writer_;
  std::string base_url_;
  std::set<MPD::AdaptionSet> adaption_set_;
  std::string format_time_now();
  void write_adaption_set(MPD::AdaptionSet & set);
  std::string write_codec(MPD::MimeType type, MPD::Representation & repr);
  void write_repr(MPD::Representation & repr);
};

#endif /* TV_ENCODER_MPD_HH */
