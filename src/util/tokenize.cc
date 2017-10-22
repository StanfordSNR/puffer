#include "tokenize.hh"

using namespace std;

vector< string > split( const string & str, const string & separator )
{
  vector< size_t > indices;

  size_t next_token = 0;
  while ( (next_token = str.find( separator, next_token )) != string::npos ) {
    indices.push_back( next_token );
    next_token++;
  }

  if ( indices.empty() ) {
    return { str };
  }

  vector< string > ret;

  /* first token */
  ret.push_back( str.substr( 0, indices[ 0 ] ) );

  /* inner tokens */
  for ( size_t i = 0; i < indices.size() - 1; i++ ) {
    ret.push_back( str.substr( indices[ i ] + separator.size(),
                  indices[ i + 1 ] - indices[ i ] - separator.size() ) );
  }

  /* last token */
  ret.push_back( str.substr( indices.back() + separator.size() ) );

  return ret;
}

pair<string, string> split_filename(const string & fname)
{
  auto pos = fname.find_last_of('.');
  if (pos == string::npos) {
    return make_pair(fname, "");
  }

  return make_pair(fname.substr(0, pos), fname.substr(pos + 1));
}
