/* -*-mode:c++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */

#include <cstdlib>
#include <iostream>

#include "file_descriptor.hh"
#include "exception.hh"

using namespace std;
const size_t ts_packet_length = 188;
const char ts_packet_sync_byte = 0x47;
const size_t packets_in_chunk = 512;

int main( int argc, char *argv[] )
{
  if ( argc != 2 ) {
    cerr << "Usage: " << argv[ 0 ] << " PID\n";
    return EXIT_FAILURE;
  }

  unsigned int pid = stoi( argv[ 1 ] );

  cerr << "Demultiplexing transport stream for pid " << pid << "...\n";

  FileDescriptor stdin { 0 };

  unsigned int packets_parsed = 0;
  int ret = EXIT_SUCCESS;

  try {
    while ( true ) {
      /* read chunks of MPEG-2 transport-stream packets in a loop */

      const string chunk = stdin.read_exactly( ts_packet_length * packets_in_chunk );
      for ( unsigned packet_no = 0; packet_no < packets_in_chunk; packet_no++ ) {
        if ( chunk[ packet_no * ts_packet_length ] != ts_packet_sync_byte ) {
          throw runtime_error( "invalid TS sync byte" );
        }

        packets_parsed++;
      }
    }
  } catch ( const exception & e ) {
    print_exception( argv[ 0 ], e );
    ret = EXIT_FAILURE;
  }

  cerr << "Packets successfully parsed: " << packets_parsed << "\n";

  return ret;
}
