/* -*-mode:c++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */

#include "config.h"

#include <cstdlib>
#include <iostream>

#ifdef HAVE_STRING_VIEW
#include <string_view>
#elif HAVE_EXPERIMENTAL_STRING_VIEW
#include <experimental/string_view>
using std::experimental::string_view;
#endif

#include "file_descriptor.hh"
#include "exception.hh"

using namespace std;

const size_t ts_packet_length = 188;
const char ts_packet_sync_byte = 0x47;
const size_t packets_in_chunk = 512;

class TSParser
{
private:
  unsigned int pid_; /* program ID of interest */

  unsigned int packets_parsed_ {}; /* count of TS packets successfully parsed */

public:
  TSParser( const unsigned int pid )
    : pid_( pid )
  {
    if ( pid >= (1 << 13) ) {
      throw runtime_error( "program ID must be less than " + to_string( 1 << 13 ) );
    }
  }

  void parse( const string_view & packet )
  {
    if ( packet.length() != ts_packet_length ) {
      throw runtime_error( "invalid TS packet length" );
    }

    if ( packet.front() != ts_packet_sync_byte ) {
      throw runtime_error( "invalid TS sync byte" );
    }

    packets_parsed_++;
  }

  /* accessors */
  unsigned int packets_parsed() const { return packets_parsed_; }
};

int main( int argc, char *argv[] )
{
  if ( argc != 2 ) {
    cerr << "Usage: " << argv[ 0 ] << " PID\n";
    return EXIT_FAILURE;
  }

  unsigned int pid = stoi( argv[ 1 ] );

  cerr << "Demultiplexing transport stream for pid " << pid << "...\n";

  FileDescriptor stdin { 0 };

  TSParser parser { pid };
  int ret = EXIT_SUCCESS;

  try {
    while ( true ) {
      /* read chunks of MPEG-2 transport-stream packets in a loop */

      const string chunk = stdin.read_exactly( ts_packet_length * packets_in_chunk );
      const string_view chunk_view { chunk.data(), chunk.size() };

      for ( unsigned packet_no = 0; packet_no < packets_in_chunk; packet_no++ ) {
        parser.parse( chunk_view.substr( packet_no * ts_packet_length,
                                         ts_packet_length ) );
      }
    }
  } catch ( const exception & e ) {
    print_exception( argv[ 0 ], e );
    ret = EXIT_FAILURE;
  }

  cerr << "Packets successfully parsed: " << parser.packets_parsed() << "\n";

  return ret;
}
