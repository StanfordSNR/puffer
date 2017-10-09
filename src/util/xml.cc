#include "xml.hh"
#include <sstream>
#include <stack>
#include <iostream>
#include <fstream>

using namespace std;


#define XML_HEADER "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
#define XML_INDENT "  "


XMLWriter::XMLWriter(): tagOpen( false ), newLine( true ),
                        os(ostringstream()), elt_stack( stack<XMLNode>() )
{
    this->os << ( XML_HEADER ) << endl;
}


XMLWriter& XMLWriter::openElt( const char *tag )
{
    this->closeTag();
    if( this->elt_stack.size() > 0 ) {
        this->os << endl;
        this->indent();
        this->elt_stack.top().hasContent = true;
    }
    this-> os << "<" << tag;
    this->elt_stack.push( XMLNode( tag ) );
    this->tagOpen = true;
    this->newLine = false;

    return *this;
}

XMLWriter& XMLWriter::closeElt()
{
    if( !this->elt_stack.size() )
        throw "XMLWriter is in an incorrect state.";
    XMLNode node = this->elt_stack.top();
    this->elt_stack.pop();
    if( !node.hasContent ){
        // no actual value, maybe just attr
        this->os << " />";
        this->tagOpen = false;
    } else {
        this->closeTag();
        if( this->newLine ){
            os << endl;
            this->indent();
        }
        this->os << "</" << node.tag << ">";
    }
    this->newLine = true;
    return *this;
}

XMLWriter& XMLWriter::closeAll()
{
    while( this->elt_stack.size() )
        this->closeElt();
    return *this;
}

XMLWriter& XMLWriter::attr( const char *key, const char *val )
{
    this->os << " " << key << "=\"";
    this->write_escape( val );
    this->os << "\"";
    return *this;
}


XMLWriter& XMLWriter::attr( const char *key, std::string val )
{
    return this->attr(key, val.c_str());
}

XMLWriter& XMLWriter::content( const char *val )
{
    this->closeTag();
    this->write_escape(val);
    this->elt_stack.top().hasContent = true;
    return *this;
}

inline void XMLWriter::closeTag()
{
    if( this->tagOpen ){
        this-> os << ">";
        this->tagOpen = false;
    }
}


inline void XMLWriter::indent()
{
    for( unsigned int i = 0; i < this->elt_stack.size(); i++ )
        os << ( XML_INDENT );
}

inline void XMLWriter::write_escape( const char *str )
{
    for ( ; *str; str++ )
        switch (*str) {
            case '&': os << "&amp;"; break;
            case '<': os << "&lt;"; break;
            case '>': os << "&gt;"; break;
            case '\'': os << "&apos;"; break;
            case '"': os << "&quot;"; break;
            default: os.put(*str); break;
        }
}

string XMLWriter::str()
{
    return this->os.str();
}

void XMLWriter::outputToFile( ofstream &out )
{
    out << this->str();
}