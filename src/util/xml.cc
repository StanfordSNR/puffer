#include "xml.hh"
#include <sstream>
#include <stack>
#include <iostream>
#include <fstream>

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

XMLWriter& XMLWriter::content(const char *val)
{
  this->close_tag();
  this->write_escape(val);
  this->elt_stack_.top().hasContent = true;
  return *this;
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
