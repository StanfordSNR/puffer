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

struct TSPacketRequirements
{
  TSPacketRequirements( const string_view & packet )
  {
    /* enforce invariants */
    if ( packet.length() != ts_packet_length ) {
      throw runtime_error( "invalid TS packet length" );
    }

    if ( packet.front() != ts_packet_sync_byte ) {
      throw runtime_error( "invalid TS sync byte" );
    }
  }
};

struct TSPacketHeader : TSPacketRequirements
{
  bool transport_error_indicator;
  bool payload_unit_start_indicator;
  uint16_t pid;
  uint8_t adaptation_field_control;
  uint8_t payload_start;

  TSPacketHeader( const string_view & packet )
    : TSPacketRequirements( packet ),
      transport_error_indicator( packet[ 1 ] & 0x80 ),
      payload_unit_start_indicator( packet[ 1 ] & 0x40 ),
      pid( ((packet[ 1 ] & 0x1f) << 8) | packet[ 2 ] ),
      adaptation_field_control( (packet[ 3 ] & 0x30) >> 4 ),
      payload_start( 4 )
  {
    /* find start of payload */
    switch ( adaptation_field_control ) {
    case 0:
      throw runtime_error( "reserved value of adaptation field control" );
    case 1:
      /* already 4 */
      break;
    case 2:
      payload_start = ts_packet_length; /* no data */
      break;
    case 3:
      const uint8_t adaptation_field_length = packet[ 4 ];
      payload_start += adaptation_field_length + 1 /* length field is 1 byte itself */;
      break;
    }

    if ( payload_start > ts_packet_length ) {
      throw runtime_error( "invalid TS packet" );
    }
  }
};

struct PESPacketHeader
{
  uint8_t stream_id;
  unsigned int payload_start;

  static uint8_t enforce_video( const uint8_t stream_id )
  {
    if ( (stream_id & 0xf0) != 0xe0 ) {
      throw runtime_error( "not an MPEG-2 video stream" );
    }

    return stream_id;
  }

  PESPacketHeader( const string_view & packet )
    : stream_id( enforce_video( packet.at( 3 ) ) ),
      payload_start( packet.at( 8 ) + 1 )
  {
    if ( packet.at( 0 ) != 0
         or packet.at( 1 ) != 0
         or packet.at( 2 ) != 1 ) {
      throw runtime_error( "invalid PES start code" );
    }

    if ( payload_start > packet.length() ) {
      throw runtime_error( "invalid PES packet" );
    }
  }
};

class TSParser
{
private:
  unsigned int pid_; /* program ID of interest */

  unsigned int packets_parsed_ {}; /* count of TS packets successfully parsed */

  string PES_packet_ {};

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
    TSPacketHeader header { packet };

    if ( header.pid != pid_ ) {
      return;
    }

    if ( header.payload_unit_start_indicator ) {
      /* start of new PES packet */

      /* step 1: parse and output old PES packet if there is one */
      if ( not PES_packet_.empty() ) {
        PESPacketHeader pes_header { PES_packet_ };

        cout << PES_packet_.substr( pes_header.payload_start );

        PES_packet_.clear();
      }

      /* step 2: start a new PES packet */
      PES_packet_.append( packet.substr( header.payload_start ) );
    } else if ( not PES_packet_.empty() ) {
      /* interior TS packet within a PES packet */
      PES_packet_.append( packet.substr( header.payload_start ) );
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
