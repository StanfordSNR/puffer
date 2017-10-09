#include "argparser.hh"
#include <set>
#include <string>
#include <iostream>
#include <iomanip>

using namespace std;

ArgParser::ArgParser( const string &name ) :
        name( name ), arguments( set<string>()),  requiredArgs( set<string>() ),
        argValues( map<string, string>() ), argHelps( map<string, string>() )
        { }



bool ArgParser::parse( int argc, const char **argv )
{
    int i = 0;
    while( ++i < argc ){
        const char *arg = argv[ i ];
        if(this->arguments.find( arg ) != this->arguments.end()){
            if( i == argc - 1 ){
                return false; // missing the last arg value
            }
            // we found one arg
            this->argValues[ arg ] = argv[ ++i ];
            this->requiredArgs.erase( arg );
        }
    }

    // if some required args is missing, an exception will be thrown
    if( !this->requiredArgs.empty() ) {
        throw this->requiredArgs;
    }
    return true;
}

bool ArgParser::addArgument( const string &arg, const string &desc )
{
    return this->addArgument( arg, desc, false );
}

bool ArgParser::addArgument( const string &arg, const string &desc, bool required)
{
    if( this->arguments.find( arg ) != this->arguments.end() )
        return false;
    this->arguments.insert( arg );
    this->argHelps[ arg ] = desc;

    if( required ) {
        this->requiredArgs.insert( arg );
    }

    return true;
}


void ArgParser::printHelp()
{
    cout << this->name << endl;
    for( auto it : this->arguments ){
        cout << "  " << it << setw( 8 ) << ": " << this->argHelps[ it ] << endl;
    }
}