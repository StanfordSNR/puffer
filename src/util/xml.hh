#ifndef TV_ENCODER_XML_HH
#define TV_ENCODER_XML_HH
#include <string>
#include <stack>
#include <sstream>

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
  XMLWriter& content(const char* val);

  std::string str();
  void output(std::ofstream &out);

  XMLWriter();
  ~XMLWriter();
};

#endif /* TV_ENCODER_XML_HH */
