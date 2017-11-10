/* -*-mode:c++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */

#include "config.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>
#include <cstring>
#include <array>

#ifdef HAVE_STRING_VIEW
#include <string_view>
#elif HAVE_EXPERIMENTAL_STRING_VIEW
#include <experimental/string_view>
using std::experimental::string_view;
#endif

extern "C" {
#include "mpeg2.h"
}
 
#include "file_descriptor.hh"
#include "exception.hh"

using namespace std;

const size_t ts_packet_length = 188;
const char ts_packet_sync_byte = 0x47;
const size_t packets_in_chunk = 512;
const size_t max_frame_size = 1048576 * 10;

template <typename T>
inline T * notnull( const string & context, T * const x )
{
  return x ? x : throw runtime_error( context + ": returned null pointer" );
}

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
  bool data_alignment_indicator;
  uint8_t PTS_DTS_flags;
  uint64_t presentation_time_stamp;
  uint64_t decoding_time_stamp;

  static uint8_t enforce_is_video( const uint8_t stream_id )
  {
    if ( (stream_id & 0xf0) != 0xe0 ) {
      throw runtime_error( "not an MPEG-2 video stream" );
    }

    return stream_id;
  }

  PESPacketHeader( const string_view & packet )
    : stream_id( enforce_is_video( packet.at( 3 ) ) ),
      payload_start( packet.at( 8 ) + 1 ),
      data_alignment_indicator( packet.at( 6 ) & 0x04 ),
      PTS_DTS_flags( (packet.at( 7 ) & 0xc0) >> 6 ),
      presentation_time_stamp(),
      decoding_time_stamp()
  {
    if ( packet.at( 0 ) != 0
         or packet.at( 1 ) != 0
         or packet.at( 2 ) != 1 ) {
      throw runtime_error( "invalid PES start code" );
    }

    if ( payload_start > packet.length() ) {
      throw runtime_error( "invalid PES packet" );
    }

    if ( not data_alignment_indicator ) {
      throw runtime_error( "unaligned PES packet" );
    }

    switch ( PTS_DTS_flags ) {
    case 0:
      throw runtime_error( "missing PTS and DTS" );
    case 1:
      throw runtime_error( "forbidden value of PTS_DTS_flags" );
    case 3:
      decoding_time_stamp = (((uint64_t(uint8_t(packet.at( 14 )) & 0x0F) >> 1) << 30) |
                             (uint8_t(packet.at( 15 )) << 22) |
                             ((uint8_t(packet.at( 16 )) >> 1) << 15) |
                             (uint8_t(packet.at( 17 )) << 7) |
                             (uint8_t(packet.at( 18 )) >> 1));

      if ( (packet.at( 14 ) & 0xf0) >> 4 != 1 ) {
        throw runtime_error( "invalid DTS prefix bits" );
      }

      if ( (packet.at( 14 ) & 0x01) != 1 ) {
        throw runtime_error( "invalid marker bit" );
      }

      if ( (packet.at( 16 ) & 0x01) != 1 ) {
        throw runtime_error( "invalid marker bit" );
      }

      if ( (packet.at( 18 ) & 0x01) != 1 ) {
        throw runtime_error( "invalid marker bit" );
      }

      /* fallthrough */

    case 2:
      presentation_time_stamp = (((uint64_t(uint8_t(packet.at( 9 )) & 0x0F) >> 1) << 30) |
                                 (uint8_t(packet.at( 10 )) << 22) |
                                 ((uint8_t(packet.at( 11 )) >> 1) << 15) |
                                 (uint8_t(packet.at( 12 )) << 7) |
                                 (uint8_t(packet.at( 13 )) >> 1));

      if ( (packet.at( 9 ) & 0xf0) >> 4 != PTS_DTS_flags ) {
        throw runtime_error( "invalid PTS prefix bits" );
      }

      if ( (packet.at( 9 ) & 0x01) != 1 ) {
        throw runtime_error( "invalid marker bit" );
      }

      if ( (packet.at( 11 ) & 0x01) != 1 ) {
        throw runtime_error( "invalid marker bit" );
      }

      if ( (packet.at( 13 ) & 0x01) != 1 ) {
        throw runtime_error( "invalid marker bit" );
      }
    }

    if ( PTS_DTS_flags == 2 ) {
      decoding_time_stamp = presentation_time_stamp;
    }
  }
};

class MPEG2VideoDecoder
{
private:
  struct MPEG2Deleter
  {
    void operator()( mpeg2dec_t * const x ) const
    {
      mpeg2_close( x );
    }
  };
  
  unique_ptr<mpeg2dec_t, MPEG2Deleter> decoder_;

  vector<uint8_t> mutable_coded_frame_;
  uint64_t last_presentation_time_stamp_ {};
  
public:
  MPEG2VideoDecoder()
    : decoder_( notnull( "mpeg2_init", mpeg2_init() ) ),
      mutable_coded_frame_( max_frame_size )
  {}

  void tag_presentation_time_stamp( const uint64_t presentation_time_stamp )
  {
    mpeg2_tag_picture( decoder_.get(),
                       presentation_time_stamp >> 32,
                       presentation_time_stamp & 0xFFFFFFFF );
  }

  void decode_frame( const string & coded_frame )
  {
    if ( coded_frame.size() > mutable_coded_frame_.size() ) {
      throw runtime_error( "coded frame too big to fit in buffer" );
    }

    memcpy( mutable_coded_frame_.data(), coded_frame.data(), coded_frame.size() );

    /* give bytes to the MPEG-2 video decoder */
    mpeg2_buffer( decoder_.get(),
                  mutable_coded_frame_.data(),
                  mutable_coded_frame_.data() + coded_frame.size() );

    /* actually decode the frame */
    unsigned int picture_count = 0;

    while ( true ) {
      mpeg2_state_t state = mpeg2_parse( decoder_.get() );
      const mpeg2_info_t * decoder_info = notnull( "mpeg2_info",
                                                   mpeg2_info( decoder_.get() ) );
      
      switch ( state ) {
      case STATE_SEQUENCE:
        {
          const mpeg2_sequence_t * sequence = notnull( "sequence",
                                                       decoder_info->sequence );
          cerr << "Sequence header: " << sequence->width << " "
               << sequence->height << " "
               << sequence->chroma_width << " "
               << sequence->chroma_height << " "
               << sequence->picture_width << " "
               << sequence->picture_height << " "
               << sequence->display_width << " "
               << sequence->display_height << " "
               << sequence->pixel_width << " "
               << sequence->pixel_height << " "
               << sequence->frame_period << "\n";
            }
        break;
      case STATE_BUFFER:
        if ( picture_count != 1 ) {
          cerr << "PES packet with picture_count = " << picture_count << "\n";
        }
        return;
      case STATE_SLICE:
        picture_count++;

        {
          const mpeg2_picture_t * pic = decoder_info->display_picture;
          if ( pic ) {
            /* picture ready for display */          
            if ( not (pic->flags & PIC_FLAG_TAGS) ) {
              throw runtime_error( "picture without timestamp" );
            } else {
              const uint64_t pts = (uint64_t( pic->tag ) << 32) | (pic->tag2);
              cerr << "PICTURE with pts delta " << pts - last_presentation_time_stamp_ << "\n";
              last_presentation_time_stamp_ = pts;

              /* write out y4m */
            }
          }
        }
        break;
      case STATE_INVALID:
      case STATE_INVALID_END:
        cerr << "invalid\n";
        break;
      case STATE_PICTURE:
        break;
      case STATE_PICTURE_2ND:
        throw runtime_error( "unsupported field pictures" );
      default:
        /* do nothing */
        break;
      }
    }
  }
};

class TSParser
{
private:
  unsigned int pid_; /* program ID of interest */

  unsigned int packets_parsed_ {}; /* count of TS packets successfully parsed */

  string PES_packet_ {};

  void append_payload( const string_view & packet, const TSPacketHeader & header )
  {
    const string_view payload = packet.substr( header.payload_start );
    PES_packet_.append( payload.begin(), payload.end() );
  }

public:
  TSParser( const unsigned int pid )
    : pid_( pid )
  {
    if ( pid >= (1 << 13) ) {
      throw runtime_error( "program ID must be less than " + to_string( 1 << 13 ) );
    }
  }

  void parse( const string_view & packet, MPEG2VideoDecoder & video_decoder )
  {
    TSPacketHeader header { packet };

    if ( header.pid != pid_ ) {
      return;
    }

    if ( header.payload_unit_start_indicator ) {
      /* start of new PES packet */

      /* step 1: parse and decode old PES packet if there is one */
      if ( not PES_packet_.empty() ) {
        PESPacketHeader pes_header { PES_packet_ };

        video_decoder.tag_presentation_time_stamp( pes_header.presentation_time_stamp );
        video_decoder.decode_frame( PES_packet_.substr( pes_header.payload_start ) );

        PES_packet_.clear();
      }

      /* step 2: start a new PES packet */
      append_payload( packet, header );
    } else if ( not PES_packet_.empty() ) {
      /* interior TS packet within a PES packet */
      append_payload( packet, header );
    }

    packets_parsed_++;
  }

  /* accessors */
  unsigned int packets_parsed() const { return packets_parsed_; }
};

int main( int argc, char *argv[] )
{
  if ( argc != 5 ) {
    cerr << "Usage: " << argv[ 0 ] << " VIDEO_PID EXPECTED_WIDTH EXPECTED_HEIGHT EXPECTED_FRAME_INTERVAL [e.g. 900900 for 30i]\n";
    return EXIT_FAILURE;
  }

  const unsigned int pid = stoi( argv[ 1 ] );
  /*
  const unsigned int expected_width = stoi( argv[ 2 ] );
  const unsigned int expected_height = stoi( argv[ 3 ] );
  const unsigned int expected_frame_interval = stoi( argv[ 4 ] );
  */

  FileDescriptor stdin { 0 };

  TSParser parser { pid };
  MPEG2VideoDecoder mpeg2_decoder_ {};

  try {
    while ( true ) {
      /* read chunks of MPEG-2 transport-stream packets in a loop */

      const string chunk = stdin.read_exactly( ts_packet_length * packets_in_chunk );
      const string_view chunk_view { chunk };

      for ( unsigned packet_no = 0; packet_no < packets_in_chunk; packet_no++ ) {
        parser.parse( chunk_view.substr( packet_no * ts_packet_length,
                                         ts_packet_length ),
                      mpeg2_decoder_ );
      }
    }
  } catch ( const exception & e ) {
    print_exception( argv[ 0 ], e );
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
