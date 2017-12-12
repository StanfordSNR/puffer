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

template <typename T>
inline T * notnull( const string & context, T * const x )
{
  return x ? x : throw runtime_error( context + ": returned null pointer" );
}

class VideoOutput
{
private:
  unsigned int display_width_;
  unsigned int display_height_;
  uint64_t frame_interval_;

  bool video_output_has_started_ {};
  uint64_t initial_presentation_time_stamp_ {};
  uint64_t field_count_ {};

  bool next_field_is_top_ { true };

  static int64_t pts_difference ( const uint64_t a, const uint64_t b )
  {
    if ( a != (a & 0x1FFFFFFFF) ) {
      throw runtime_error( "invalid pts a" );
    }

    if ( b != (b & 0x1FFFFFFFF) ) {
      throw runtime_error( "invalid pts b" );
    }

    const int64_t difference = int64_t( a ) - int64_t( b );

    const int64_t mod = uint64_t(1) << 33;
    int64_t mod_difference = ((difference % mod) + mod) % mod;

    if ( mod_difference > (mod>>1) ) {
      mod_difference -= mod;
    }

    return mod_difference;
  }
  
  FileDescriptor output_ { STDOUT_FILENO };

public:
  VideoOutput( const unsigned int expected_display_width,
               const unsigned int expected_display_height,
               const unsigned int expected_frame_interval )
    : display_width_( expected_display_width ),
      display_height_( expected_display_height ),
      frame_interval_( expected_frame_interval )
  {
    switch ( frame_interval_ ) {
    case 900900:
    case 450450:
      break;
    default:
      throw runtime_error( "unhandled frame interval: " + to_string( expected_frame_interval ) );
    }

    output_.write( "YUV4MPEG2 W" + to_string( display_width_ )
                   + " H" + to_string( display_height_ / 2 ) + " " + "F60000:1001"
                   + " Ip A1:1 C420mpeg2\n" );
  }

  void write_picture( const uint64_t presentation_time_stamp,
                      const uint8_t number_of_fields,
                      const bool top_field_first,
                      const mpeg2_fbuf_t * const display_raster,
                      const unsigned int physical_luma_width,
                      const unsigned int physical_chroma_width )
  {
    if ( not video_output_has_started_ ) {
      video_output_has_started_ = true;
      initial_presentation_time_stamp_ = presentation_time_stamp;
      if ( not top_field_first ) {
        throw runtime_error( "can't start video output with bottom field" );
      }
    } else {
      const uint64_t expected_presentation_time_stamp =
        (initial_presentation_time_stamp_ + ((frame_interval_ * field_count_) / 600))
        & 0x1FFFFFFFF;
      const int64_t pts_diff = pts_difference( presentation_time_stamp,
                                               expected_presentation_time_stamp );
      if ( abs( pts_diff ) > 10 ) {
        throw runtime_error( "unexpected gap in presentation timestamps: " + to_string( pts_diff ) );
      }
    }

    if ( next_field_is_top_ != top_field_first ) {
      throw runtime_error( "frame cadence mismatch" );
    }

    /* output the frame */
    for ( unsigned int field = 0; field < number_of_fields; field++ ) {
      /* write out y4m */
      output_.write( "FRAME\n" );

      /* Y */
      for ( unsigned int row = next_field_is_top_ ? 0 : 1; row < display_height_; row += 2 ) {
        const string_view the_row( reinterpret_cast<char *>( display_raster->buf[ 0 ] + row * physical_luma_width ),
                                   display_width_ );
        output_.write( the_row );
      }

      /* Cb */
      for ( unsigned int row = next_field_is_top_ ? 0 : 1; row < (1 + display_height_) / 2; row += 2 ) {
        const string_view the_row( reinterpret_cast<char *>( display_raster->buf[ 1 ] + row * physical_chroma_width ),
                                   (1 + display_width_ ) / 2 );
        output_.write( the_row );
      }
              
      /* Cr */
      for ( unsigned int row = next_field_is_top_ ? 0 : 1; row < (1 + display_height_) / 2; row += 2 ) {
        const string_view the_row( reinterpret_cast<char *>( display_raster->buf[ 2 ] + row * physical_chroma_width ),
                                   (1 + display_width_ ) / 2 );
        output_.write( the_row );
      }

      next_field_is_top_ = !next_field_is_top_;
      field_count_++;
    }
  }
};

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
      pid( ((uint8_t( packet[ 1 ] ) & 0x1f) << 8) | uint8_t( packet[ 2 ] ) ),
      adaptation_field_control( (uint8_t( packet[ 3 ] ) & 0x30) >> 4 ),
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

  unsigned int display_width_;
  unsigned int display_height_;
  unsigned int frame_interval_;

  static unsigned int macroblock_dimension( const unsigned int num )
  {
    return ( num + 15 ) / 16;
  }

    unsigned int physical_luma_width() const
  {
    return macroblock_dimension( display_width_ ) * 16;
  }

  unsigned int physical_luma_height() const
  {
    return macroblock_dimension( display_height_ ) * 16;
  }

  unsigned int physical_chroma_width() const
  {
    return (1 + physical_luma_width()) / 2;
  }
  
  unsigned int physical_chroma_height() const
  {
    return (1 + physical_luma_height()) / 2;
  }

  unsigned int display_luma_width() const
  {
    return display_width_;
  }
  
  unsigned int display_luma_height() const
  {
    return display_height_;
  }
  
  unsigned int display_chroma_width() const
  {
    return (1 + display_width_) / 2;
  }
  
  unsigned int display_chroma_height() const
  {
    return (1 + display_height_) / 2;
  }

  void enforce_as_expected( const mpeg2_sequence_t * sequence ) const
  {
    if ( sequence->width != physical_luma_width() ) {
      throw runtime_error( "width mismatch" );
    }

    if ( sequence->height != physical_luma_height() ) {
      throw runtime_error( "height mismatch" );
    }

    if ( sequence->chroma_width != physical_chroma_width() ) {
      throw runtime_error( "chroma width mismatch" );
    }

    if ( sequence->chroma_height != physical_chroma_height() ) {
      throw runtime_error( "chroma height mismatch" );
    }

    if ( sequence->picture_width != display_luma_width() ) {
      throw runtime_error( "picture width mismatch" );
    }

    if ( sequence->picture_height != display_luma_height() ) {
      throw runtime_error( "picture height mismatch" );
    }

    if ( sequence->display_width != display_luma_width() ) {
      throw runtime_error( "display width mismatch" );
    }

    if ( sequence->display_height != display_luma_height() ) {
      throw runtime_error( "display height mismatch" );
    }

    if ( sequence->pixel_width != 1
         or sequence->pixel_height != 1 ) {
      throw runtime_error( "non-square pels" );
    }
    
    if ( sequence->frame_period != frame_interval_ ) {
      throw runtime_error( "frame interval mismatch" );
    }
  }

  void output_picture( const mpeg2_picture_t * pic,
                       const mpeg2_fbuf_t * display_raster,
                       VideoOutput & output )
  {
    if ( not (pic->flags & PIC_FLAG_TAGS) ) {
      throw runtime_error( "picture without timestamp" );
    }

    const uint64_t pts = (uint64_t( pic->tag ) << 32) | (pic->tag2);

    output.write_picture( pts,
                          pic->nb_fields,
                          pic->flags & PIC_FLAG_TOP_FIELD_FIRST,
                          display_raster,
                          physical_luma_width(),
                          physical_chroma_width() );
  }

public:
  MPEG2VideoDecoder( const unsigned int expected_display_width,
                     const unsigned int expected_display_height,
                     const unsigned int expected_frame_interval )
    : decoder_( notnull( "mpeg2_init", mpeg2_init() ) ),
      display_width_( expected_display_width ),
      display_height_( expected_display_height ),
      frame_interval_( expected_frame_interval )
  {}

  void decode_frame( pair<uint64_t, string> & PES_packet, VideoOutput & output )
  {
    const uint64_t presentation_time_stamp = PES_packet.first;

    mpeg2_tag_picture( decoder_.get(),
                       presentation_time_stamp >> 32,
                       presentation_time_stamp & 0xFFFFFFFF );

    /* give bytes to the MPEG-2 video decoder */
    mpeg2_buffer( decoder_.get(),
                  reinterpret_cast<uint8_t *>( PES_packet.second.data() ),
                  reinterpret_cast<uint8_t *>( PES_packet.second.data() + PES_packet.second.size() ) );

    /* actually decode the frame */
    unsigned int picture_count = 0;

    while ( true ) {
      mpeg2_state_t state = mpeg2_parse( decoder_.get() );
      const mpeg2_info_t * decoder_info = notnull( "mpeg2_info",
                                                   mpeg2_info( decoder_.get() ) );
      
      switch ( state ) {
      case STATE_SEQUENCE:
      case STATE_SEQUENCE_REPEATED:
        {
          const mpeg2_sequence_t * sequence = notnull( "sequence",
                                                       decoder_info->sequence );
          enforce_as_expected( sequence );
        }
        break;
      case STATE_BUFFER:
        if ( picture_count > 1 ) {
          throw runtime_error( "PES packet with multiple pictures" );
        }
        return;
      case STATE_SLICE:
        picture_count++;

        {
          const mpeg2_picture_t * pic = decoder_info->display_picture;
          if ( pic ) {
            /* picture ready for display */          
            const mpeg2_fbuf_t * display_raster = notnull( "display_fbuf", decoder_info->display_fbuf );
            output_picture( pic, display_raster, output );
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

  void parse( const string_view & packet, vector<pair<uint64_t, string>> & video_PES_packets )
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

        video_PES_packets.emplace_back( pes_header.presentation_time_stamp,
                                        move( PES_packet_ ) );
        PES_packet_.clear();
      }

      /* step 2: start a new PES packet */
      append_payload( packet, header );
    } else if ( not PES_packet_.empty() ) {
      /* interior TS packet within a PES packet */
      append_payload( packet, header );
    }
  }
};

int main( int argc, char *argv[] )
{
  if ( argc != 5 ) {
    cerr << "Usage: " << argv[ 0 ] << " VIDEO_PID EXPECTED_WIDTH EXPECTED_HEIGHT EXPECTED_FRAME_INTERVAL [e.g. 900900 for 30i]\n";
    return EXIT_FAILURE;
  }

  const unsigned int pid = stoi( argv[ 1 ] );
  const unsigned int expected_width = stoi( argv[ 2 ] );
  const unsigned int expected_height = stoi( argv[ 3 ] );
  const unsigned int expected_frame_interval = stoi( argv[ 4 ] );

  FileDescriptor stdin { 0 };

  TSParser parser { pid };
  MPEG2VideoDecoder video_decoder { expected_width, expected_height, expected_frame_interval };
  VideoOutput video_output { expected_width, expected_height, expected_frame_interval };

  vector<pair<uint64_t, string>> video_PES_packets;

  try {
    while ( true ) {
      /* parse transport stream packets into video (and eventually audio) PES packets */

      const string chunk = stdin.read_exactly( ts_packet_length * packets_in_chunk );
      const string_view chunk_view { chunk };

      for ( unsigned packet_no = 0; packet_no < packets_in_chunk; packet_no++ ) {
        parser.parse( chunk_view.substr( packet_no * ts_packet_length,
                                         ts_packet_length ),
                      video_PES_packets );
      }

      /* decode video */
      for ( auto & video_PES_packet : video_PES_packets ) {
        video_decoder.decode_frame( video_PES_packet, video_output );
      }

      video_PES_packets.clear();
    }
  } catch ( const exception & e ) {
    print_exception( argv[ 0 ], e );
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
