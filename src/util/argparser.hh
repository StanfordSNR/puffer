#ifndef TV_ENCODER_ARGPARSER_HH
#define TV_ENCODER_ARGPARSER_HH

#include <string>
#include <set>
#include <map>
#include <sstream>

using namespace std;

class ArgParser
{
private:
    string name;
    set<string> arguments;
    set<string> requiredArgs;
    map<string, string> argValues;
    map<string, string> argHelps;

public:
    ArgParser( const string &name );
    bool parse( int argc, const char* argv[] );
    template<typename T> T getArgValue( const string &arg, const T &defaultValue )
    {
        if( this->arguments.find( arg ) == this->arguments.end() )
            return defaultValue;
        else
        if( this->argValues.find( arg ) == this->argValues.end() )
            return defaultValue;
        else {
            string argValue = this->argValues[arg];
            stringstream  stream( argValue );
            // try to convert type
            T value;
            stream >> value;
            if ( stream.fail() )
                return defaultValue;
            return value;
        }

    }
    bool addArgument( const string &arg, const string &desc );
    bool addArgument( const string &arg, const string &desc, bool required );
    void printHelp();
};

#endif //TV_ENCODER_ARGPARSER_HH
