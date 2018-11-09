/* -*-mode:c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include "strict_conversions.hh"

#include <iomanip>
#include "exception.hh"

using namespace std;

long int strict_atoi( const string & str, const int base )
{
    if ( str.empty() ) {
        throw runtime_error( "Invalid integer string: empty" );
    }

    char *end;

    errno = 0;
    long int ret = strtol( str.c_str(), &end, base );

    if ( errno != 0 ) {
        throw unix_error( "strtol" );
    } else if ( end != str.c_str() + str.size() ) {
        throw runtime_error( "Invalid integer: " + str );
    }

    return ret;
}

double strict_atof( const string & str )
{
    if ( str.empty() ) {
        throw runtime_error( "Invalid floating-point string: empty" );
    }

    char *end;

    errno = 0;
    double ret = strtod( str.c_str(), &end );

    if ( errno != 0 ) {
        throw unix_error( "strtod" );
    } else if ( end != str.c_str() + str.size() ) {
        throw runtime_error( "Invalid floating-point number: " + str );
    }

    return ret;
}

unsigned long int strict_atoui(const string & str, const int base)
{
  if (str.empty()) {
    throw runtime_error("Invalid integer string: empty");
  }

  char * end;

  errno = 0;
  unsigned long int ret = strtoul(str.c_str(), &end, base);

  if (errno != 0) {
    throw unix_error("strtoul");
  } else if (end != str.c_str() + str.size()) {
    throw runtime_error("Invalid integer: " + str);
  }

  return ret;
}

string double_to_string(const double input, const int precision)
{
  stringstream stream;
  stream << fixed << setprecision(precision) << input;
  return stream.str();
}
