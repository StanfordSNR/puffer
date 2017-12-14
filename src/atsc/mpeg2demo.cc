/* -*-mode:c++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */

#include "config.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>
#include <cstring>
#include <array>
#include <queue>

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

struct Raster
{
  unsigned int width, height;
  unique_ptr<uint8_t[]> Y, Cb, Cr;

  Raster( const unsigned int s_width,
          const unsigned int s_height )
    : width( s_width ),
      height( s_height ),
      Y(  make_unique<uint8_t[]>(  height    *  width ) ),
      Cb( make_unique<uint8_t[]>( (height/2) * (width/2) ) ),
      Cr( make_unique<uint8_t[]>( (height/2) * (width/2) ) )
  {
    if ( (height % 2 != 0)
         or (width % 2 != 0) ) {
      throw runtime_error( "width or height is not multiple of 2" );
    }
  }
};

struct FieldBuffer : public Raster
{
  using Raster::Raster;

  void read_from_frame( const bool top_field,
                        const unsigned int physical_luma_width,
                        const mpeg2_fbuf_t * display_raster )
  {
    if ( physical_luma_width < width ) {
      throw runtime_error( "invalid physical_luma_width" );
    }

    /* copy Y */
    for ( unsigned int source_row = (top_field ? 0 : 1), dest_row = 0;
          dest_row < height;
          source_row += 2, dest_row += 1 ) {
      memcpy( Y.get() + dest_row * width,
              display_raster->buf[ 0 ] + source_row * physical_luma_width,
              width );
    }

    /* copy Cb */
    for ( unsigned int source_row = (top_field ? 0 : 1), dest_row = 0;
          dest_row < height/2;
          source_row += 2, dest_row += 1 ) {
      memcpy( Cb.get() + dest_row * width/2,
              display_raster->buf[ 1 ] + source_row * physical_luma_width/2,
              width/2 );
    }

    /* copy Cr */
    for ( unsigned int source_row = (top_field ? 0 : 1), dest_row = 0;
          dest_row < height/2;
          source_row += 2, dest_row += 1 ) {
      memcpy( Cr.get() + dest_row * width/2,
              display_raster->buf[ 2 ] + source_row * physical_luma_width/2,
              width/2 );
    }
  }
};

class FieldBufferPool;

class FieldBufferDeleter
{
private:
  FieldBufferPool * buffer_pool_ = nullptr;

public:
  void operator()( FieldBuffer * buffer ) const;

  void set_buffer_pool( FieldBufferPool * pool );
};

typedef unique_ptr<FieldBuffer, FieldBufferDeleter> FieldBufferHandle;

class FieldBufferPool
{
private:
  queue<FieldBufferHandle> unused_buffers_ {};

public:
  FieldBufferHandle make_buffer( const unsigned int luma_width,
                                 const unsigned int field_luma_height )
  {
    FieldBufferHandle ret;

    if ( unused_buffers_.empty() ) {
      ret.reset( new FieldBuffer( luma_width, field_luma_height ) );
    } else {
      if ( (unused_buffers_.front()->width != luma_width)
           or (unused_buffers_.front()->height != field_luma_height) ) {
        throw runtime_error( "buffer size has changed" );
      }

      ret = move( unused_buffers_.front() );
      unused_buffers_.pop();
    }

    ret.get_deleter().set_buffer_pool( this );
    return ret;
  }

  void free_buffer( FieldBuffer * buffer )
  {
    if ( not buffer ) {
      throw runtime_error( "attempt to free null buffer" );
    }

    unused_buffers_.emplace( buffer );
  }
};

void FieldBufferDeleter::operator()( FieldBuffer * buffer ) const
{
  if ( buffer_pool_ ) {
    buffer_pool_->free_buffer( buffer );
  } else {
    delete buffer;
  }
}

void FieldBufferDeleter::set_buffer_pool( FieldBufferPool * pool )
{
  if ( buffer_pool_ ) {
    throw runtime_error( "buffer_pool already set" );
  }

  buffer_pool_ = pool;
}

FieldBufferPool & global_buffer_pool()
{
  static FieldBufferPool pool;
  return pool;
}

struct VideoField
{
  uint64_t presentation_time_stamp;
  bool top_field;
  FieldBufferHandle contents;

  VideoField( const uint64_t presentation_time_stamp,
              const bool top_field,
              const unsigned int luma_width,
              const unsigned int frame_luma_height,
              const unsigned int physical_luma_width,
              const mpeg2_fbuf_t * display_raster )
    : presentation_time_stamp( presentation_time_stamp ),
      top_field( top_field ),
      contents( global_buffer_pool().make_buffer( luma_width, frame_luma_height / 2 ) )
  {
    contents->read_from_frame( top_field, physical_luma_width, display_raster );
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

struct TimestampedPESPacket
{
  uint64_t presentation_time_stamp;
  size_t payload_start_index;
  string PES_packet;

  TimestampedPESPacket( const uint64_t s_presentation_time_stamp,
                        const size_t s_payload_start_index,
                        string && s_PES_packet )
    : presentation_time_stamp( s_presentation_time_stamp ),
      payload_start_index( s_payload_start_index ),
      PES_packet( move( s_PES_packet ) )
  {
    if ( payload_start_index >= PES_packet.size() ) {
      throw runtime_error( "empty PES payload" );
    }
  }

  uint8_t * payload_start()
  {
    return reinterpret_cast<uint8_t *>( PES_packet.data() + payload_start_index );
  }

  uint8_t * payload_end() /* XXX for audio, need smarter end logic */
  {
    return reinterpret_cast<uint8_t *>( PES_packet.data() + PES_packet.size() );
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

struct VideoParameters
{
  unsigned int width {};
  unsigned int height {};
  unsigned int frame_interval {};
  string y4m_description {};
  bool progressive {};

  VideoParameters( const string & format )
  {
    if ( format == "1080i30" ) {
      width = 1920;
      height = 1080;
      frame_interval = 900900;
      y4m_description = "F30000:1001 It";
      progressive = false;
    } else if ( format == "720p60" ) {
      width = 1280;
      height = 720;
      frame_interval = 450450;
      y4m_description = "F60000:1001 Ip";
      progressive = true;
    } else {
      throw runtime_error( "unsupported format: " + format );
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
  bool progressive_sequence_;

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

  void enforce_as_expected( const mpeg2_sequence_t * sequence ) const
  {
    if ( not (sequence->flags & SEQ_FLAG_MPEG2) ) {
      throw runtime_error( "sequence not flagged as MPEG-2 part 2 video" );
    }

    if ( (sequence->flags & SEQ_FLAG_PROGRESSIVE_SEQUENCE) != (SEQ_FLAG_PROGRESSIVE_SEQUENCE * progressive_sequence_) ) {
      throw runtime_error( "progressive/interlaced sequence mismatch" );
    }

    if ( sequence->width != physical_luma_width() ) {
      throw runtime_error( "width mismatch" );
    }

    if ( sequence->height != physical_luma_height() ) {
      throw runtime_error( "height mismatch" );
    }

    if ( sequence->chroma_width != physical_luma_width() / 2 ) {
      throw runtime_error( "chroma width mismatch" );
    }

    if ( sequence->chroma_height != physical_luma_height() / 2 ) {
      throw runtime_error( "chroma height mismatch" );
    }

    if ( sequence->picture_width != display_width_ ) {
      throw runtime_error( "picture width mismatch" );
    }

    if ( sequence->picture_height != display_height_ ) {
      throw runtime_error( "picture height mismatch" );
    }

    if ( sequence->display_width != display_width_ ) {
      throw runtime_error( "display width mismatch" );
    }

    if ( sequence->display_height != display_height_ ) {
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
                       queue<VideoField> & output )
  {
    if ( not (pic->flags & PIC_FLAG_TAGS) ) {
      throw runtime_error( "picture without timestamp" );
    }

    if ( progressive_sequence_ ) {
      if ( pic->nb_fields % 2 != 0 ) {
        throw runtime_error( "progressive sequence, but picture has odd number of fields" );
      }
    }

    bool next_field_is_top = (pic->flags & PIC_FLAG_TOP_FIELD_FIRST) | progressive_sequence_;
    uint64_t presentation_time_stamp = (uint64_t( pic->tag ) << 32) | (pic->tag2);

    /* output each field */
    for ( unsigned int field = 0; field < pic->nb_fields; field++ ) {
      output.emplace( presentation_time_stamp,
                      next_field_is_top,
                      display_width_,
                      display_height_,
                      physical_luma_width(),
                      display_raster );

      next_field_is_top = !next_field_is_top;
    }
  }

public:
  MPEG2VideoDecoder( const VideoParameters & params )
    : decoder_( notnull( "mpeg2_init", mpeg2_init() ) ),
      display_width_( params.width ),
      display_height_( params.height ),
      frame_interval_( params.frame_interval ),
      progressive_sequence_( params.progressive )
  {
    if ( (display_width_ % 4 != 0)
         or (display_height_ % 4 != 0) ) {
      throw runtime_error( "width or height is not multiple of 4" );
    }
  }

  void decode_frame( TimestampedPESPacket & PES_packet, /* mutable because mpeg2_buffer args are not const */
                     queue<VideoField> & output )
  {
    mpeg2_tag_picture( decoder_.get(),
                       PES_packet.presentation_time_stamp >> 32,
                       PES_packet.presentation_time_stamp & 0xFFFFFFFF );

    /* give bytes to the MPEG-2 video decoder */
    mpeg2_buffer( decoder_.get(),
                  PES_packet.payload_start(),
                  PES_packet.payload_end() );

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
        cerr << "invalid state\n";
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

  void parse( const string_view & packet, vector<TimestampedPESPacket> & video_PES_packets )
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
                                        pes_header.payload_start,
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

class YUV4MPEGPipeOutput
{
private:
  FileDescriptor output_ { STDOUT_FILENO };
  bool next_field_is_top_ { true };
  Raster pending_frame;

  void write_frame()
  {
    /* write out y4m */
    output_.write( "FRAME\n" );

    /* Y */
    output_.write( string_view { reinterpret_cast<char *>( pending_frame.Y.get() ), pending_frame.width * pending_frame.height } );

    /* Cb */
    output_.write( string_view { reinterpret_cast<char *>( pending_frame.Cb.get() ), (pending_frame.width/2) * (pending_frame.height/2) } );

    /* Cr */
    output_.write( string_view { reinterpret_cast<char *>( pending_frame.Cr.get() ), (pending_frame.width/2) * (pending_frame.height/2) } );
  }

public:
  YUV4MPEGPipeOutput( const VideoParameters & params )
    : pending_frame( params.width, params.height )
  {
    output_.write( "YUV4MPEG2 W" + to_string( params.width )
                   + " H" + to_string( params.height ) + " " + params.y4m_description
                   + " A1:1 C420mpeg2\n" );
  }

  void write( const VideoField & field )
  {
    if ( field.top_field != next_field_is_top_ ) {
      cerr << "skipping field with wrong parity\n";
      return;
    }

    /* copy field to proper lines of pending frame */

    /* copy Y */
    for ( unsigned int source_row = 0, dest_row = (next_field_is_top_ ? 0 : 1);
          source_row < field.contents->height;
          source_row += 1, dest_row += 2 ) {
      memcpy( pending_frame.Y.get() + dest_row * pending_frame.width,
              field.contents->Y.get() + source_row * pending_frame.width,
              pending_frame.width );
    }

    /* copy Cb */
    for ( unsigned int source_row = 0, dest_row = (next_field_is_top_ ? 0 : 1);
          source_row < field.contents->height/2;
          source_row += 1, dest_row += 2 ) {
      memcpy( pending_frame.Cb.get() + dest_row * pending_frame.width/2,
              field.contents->Cb.get() + source_row * pending_frame.width/2,
              pending_frame.width/2 );
    }

    /* copy Cr */
    for ( unsigned int source_row = 0, dest_row = (next_field_is_top_ ? 0 : 1);
          source_row < field.contents->height/2;
          source_row += 1, dest_row += 2 ) {
      memcpy( pending_frame.Cr.get() + dest_row * pending_frame.width/2,
              field.contents->Cr.get() + source_row * pending_frame.width/2,
              pending_frame.width/2 );
    }

    next_field_is_top_ = !next_field_is_top_;

    if ( next_field_is_top_ ) {
      /* print out frame */
      write_frame();
    }
  }
};

int main( int argc, char *argv[] )
{
  if ( argc != 3 ) {
    cerr << "Usage: " << argv[ 0 ] << " pid format\n\n   format = \"1080i30\" | \"720p60\"\n";
    return EXIT_FAILURE;
  }

  /* NB: "1080i30" is the preferred notation in Poynton's books and "Video Demystified" */

  const unsigned int pid = stoi( argv[ 1 ] );
  const VideoParameters params { argv[ 2 ] };

  FileDescriptor stdin { 0 };

  TSParser parser { pid };
  vector<TimestampedPESPacket> video_PES_packets; /* output of TSParser */

  MPEG2VideoDecoder video_decoder { params };
  queue<VideoField> decoded_fields; /* output of MPEG2VideoDecoder */

  YUV4MPEGPipeOutput y4m_output { params };

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
        video_decoder.decode_frame( video_PES_packet, decoded_fields );
      }

      video_PES_packets.clear();

      /* output fields? */
      while ( not decoded_fields.empty() ) {
        y4m_output.write( decoded_fields.front() );
        decoded_fields.pop();
      }
    }
  } catch ( const exception & e ) {
    print_exception( argv[ 0 ], e );
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
