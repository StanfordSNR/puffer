#ifndef TV_ENCODER_XML_HH
#define TV_ENCODER_XML_HH
#include <string>
#include <stack>
#include <sstream>

using namespace std;


class XMLNode
{
public:
    const char *tag;
    bool hasContent;
    XMLNode( const char *tag, bool hasContent): tag( tag ), hasContent( hasContent ) { }
    XMLNode( const char *tag): XMLNode( tag, false ) { }
};

// based on
// https://gist.github.com/sebclaeys/1227644
// significant improvements are made

class XMLWriter
{
private:
    bool tagOpen;
    bool newLine;
    ostringstream os;
    stack<XMLNode> elt_stack;
    inline void closeTag();
    inline void indent();
    inline void write_escape( const char* str );

public:
    XMLWriter& openElt( const char* tag );
    XMLWriter& closeElt();
    XMLWriter& closeAll();
    XMLWriter& attr( const char* key, const char* val );
    XMLWriter& attr( const char* key, std::string val );
    XMLWriter& content( const char* val);

    string str();
    void outputToFile( ofstream &out );

    XMLWriter();
};



#endif //TV_ENCODER_XML_HH
