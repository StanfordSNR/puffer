/* -*-mode:c++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */

#include "config.h"

#include <getopt.h>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>
#include <cstring>
#include <array>
#include <queue>
#include <optional>
#include <cmath>

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#ifdef HAVE_STRING_VIEW
#include <string_view>
#elif HAVE_EXPERIMENTAL_STRING_VIEW
#include <experimental/string_view>
using std::experimental::string_view;
#endif

extern "C" {
#include <mpeg2.h>
#include <a52dec/a52.h>
#include <a52dec/mm_accel.h>
}

#include "file_descriptor.hh"
#include "exception.hh"
#include "filesystem.hh"
#include "strict_conversions.hh"
#include "socket.hh"
#include "timestamp.hh"
#include "poller.hh"

using namespace std;
using namespace PollerShortNames;

static const size_t ts_packet_length = 188;
static const char ts_packet_sync_byte = 0x47;
static const unsigned int atsc_audio_sample_rate = 48000;
static const unsigned int audio_block_duration = 144000;
/* units -v '(256 / (48 kHz)) * (27 megahertz)' -> 144000 */
static const unsigned int audio_samples_per_block = 256;
static const unsigned int opus_sample_overlap = 10 * 960 + 960 - 312; /* 960 = 48 kHz * 20 ms, 312 = Opus's 6.5 ms lookahead */

/* if tmp_dir is not empty, output to tmp_dir first and move output chunks
 * to video_output_dir or audio_output_dir */
static string tmp_dir;

void print_usage( const string & program_name )
{
  cerr <<
  "Usage: " << program_name << " video_pid audio_pid format "
  "frames_per_chunk audio_blocks_per_chunk audio_sample_overlap "
  "video_output_dir audio_output_dir [--tmp TMP] [--tcp IP:PORT]\n\n"
  "format = \"1080i30\" | \"720p60\"\n"
  "--tmp TMP : output to TMP directory first and then move output chunks "
  "to video_output_dir or audio_output_dir\n"
  "--tcp IP:PORT : establish a TCP connection and read input from IP:PORT"
  << endl;
}

template <typename T>
inline T * notnull( const string & context, T * const x )
{
  return x ? x : throw runtime_error( context + ": returned null pointer" );
}

int64_t timestamp_difference( const uint64_t ts_64, const uint64_t ts_33 )
{
  return static_cast<int64_t>(ts_64) - static_cast<int64_t>(ts_33);
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

    clear();
  }

  void clear()
  {
    memset( Y.get(),  16,   height    *  width );
    memset( Cb.get(), 128, (height/2) * (width/2) );
    memset( Cr.get(), 128, (height/2) * (width/2) );
  }

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

class non_fatal_exception : public runtime_error
{
public:
  using runtime_error::runtime_error;
};

class InvalidMPEG : public non_fatal_exception
{
public:
  using non_fatal_exception::non_fatal_exception;
};

class UnsupportedMPEG : public non_fatal_exception
{
public:
  using non_fatal_exception::non_fatal_exception;
};

class StreamMismatch : public non_fatal_exception
{
public:
  using non_fatal_exception::non_fatal_exception;
};

class HugeTimestampDifference : public non_fatal_exception
{
public:
  using non_fatal_exception::non_fatal_exception;
};

class RasterPool;

class RasterDeleter
{
private:
  RasterPool * buffer_pool_ = nullptr;

public:
  void operator()( Raster * buffer ) const;

  void set_buffer_pool( RasterPool * pool );
};

typedef unique_ptr<Raster, RasterDeleter> RasterHandle;

class RasterPool
{
private:
  queue<RasterHandle> unused_buffers_ {};

public:
  RasterHandle make_buffer( const unsigned int luma_width,
                            const unsigned int field_luma_height )
  {
    RasterHandle ret;

    if ( unused_buffers_.empty() ) {
      ret.reset( new Raster( luma_width, field_luma_height ) );
    } else {
      if ( (unused_buffers_.front()->width != luma_width)
           or (unused_buffers_.front()->height != field_luma_height) ) {
        throw runtime_error( "buffer size has changed" );
      }

      ret = move( unused_buffers_.front() );
      unused_buffers_.pop();
      ret->clear();
    }

    ret.get_deleter().set_buffer_pool( this );
    return ret;
  }

  void free_buffer( Raster * buffer )
  {
    if ( not buffer ) {
      throw runtime_error( "attempt to free null buffer" );
    }

    unused_buffers_.emplace( buffer );
  }
};

void RasterDeleter::operator()( Raster * buffer ) const
{
  if ( buffer_pool_ ) {
    buffer_pool_->free_buffer( buffer );
  } else {
    delete buffer;
  }
}

void RasterDeleter::set_buffer_pool( RasterPool * pool )
{
  if ( buffer_pool_ ) {
    throw runtime_error( "buffer_pool already set" );
  }

  buffer_pool_ = pool;
}

RasterPool & global_buffer_pool()
{
  static RasterPool pool;
  return pool;
}

struct VideoField
{
  uint64_t presentation_time_stamp;
  bool top_field;
  RasterHandle contents;

  VideoField( const uint64_t presentation_time_stamp,
              const bool top_field,
              const unsigned int luma_width,
              const unsigned int frame_luma_height,
              const unsigned int physical_luma_width,
              const mpeg2_fbuf_t * display_raster )
    : VideoField( presentation_time_stamp,
                  top_field,
                  luma_width,
                  frame_luma_height )
  {
    contents->read_from_frame( top_field, physical_luma_width, display_raster );
  }

  VideoField( const uint64_t presentation_time_stamp,
              const bool top_field,
              const unsigned int luma_width,
              const unsigned int frame_luma_height )
    : presentation_time_stamp( presentation_time_stamp ),
      top_field( top_field ),
      contents( global_buffer_pool().make_buffer( luma_width, frame_luma_height / 2 ) )
  {}
};

struct TSPacketRequirements
{
  TSPacketRequirements( const string_view & packet )
  {
    /* enforce invariants */
    if ( packet.length() != ts_packet_length ) {
      throw InvalidMPEG( "invalid TS packet length" );
    }

    if ( packet.front() != ts_packet_sync_byte ) {
      throw InvalidMPEG( "invalid TS sync byte" );
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
      throw UnsupportedMPEG( "reserved value of adaptation field control" );
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
      throw InvalidMPEG( "invalid TS packet" );
    }
  }
};

struct TimestampedPESPacket
{
  uint64_t presentation_time_stamp;
  size_t payload_start_index;
  size_t payload_end_index;
  string PES_packet;

  TimestampedPESPacket( const uint64_t s_presentation_time_stamp,
                        const size_t s_payload_start_index,
                        const size_t PES_packet_length,
                        string && s_PES_packet )
    : presentation_time_stamp( s_presentation_time_stamp ),
      payload_start_index( s_payload_start_index ),
      payload_end_index( s_PES_packet.size() ),
      PES_packet( move( s_PES_packet ) )
  {
    if ( payload_start_index >= PES_packet.size() ) {
      throw InvalidMPEG( "empty PES payload" );
    }

    if ( PES_packet_length != 0 ) {
      if ( PES_packet_length + 6 > PES_packet.size() ) {
        throw InvalidMPEG( "PES_packet_length + 6 > PES_packet.size()" );
      }

      payload_end_index = PES_packet_length + 6;
    }
  }

  uint8_t * payload_start()
  {
    if ( payload_start_index >= PES_packet.length()
         or payload_start_index >= payload_end_index ) {
      throw InvalidMPEG( "payload starts after end of PES packet" );
    }
    return reinterpret_cast<uint8_t *>( PES_packet.data() + payload_start_index );
  }

  uint8_t * payload_end()
  {
    if ( payload_end_index > PES_packet.length() ) {
      throw InvalidMPEG( "payload ends after end of PES packet" );
    }
    return reinterpret_cast<uint8_t *>( PES_packet.data() + payload_end_index );
  }

  size_t payload_length() const
  {
    if ( payload_end_index < payload_start_index ) {
      throw InvalidMPEG( "payload ends before it starts" );
    }
    return payload_end_index - payload_start_index;
  }
};

struct PESPacketHeader
{
  uint8_t stream_id;
  unsigned int payload_start;
  unsigned int PES_packet_length;
  bool data_alignment_indicator;
  uint8_t PTS_DTS_flags;
  uint64_t presentation_time_stamp;
  uint64_t decoding_time_stamp;

  static uint8_t enforce_stream_id( const bool is_video, const uint8_t stream_id )
  {
    if ( is_video ) {
      if ( (stream_id & 0xf0) != 0xe0 ) {
        throw StreamMismatch( "not an MPEG-2 video stream: " + to_string( stream_id ) );
      }
    } else {
      if ( stream_id != 0xBD ) {
        throw StreamMismatch( "not an A/52 audio stream: " + to_string( stream_id ) );
      }
    }

    return stream_id;
  }

  PESPacketHeader( const string_view & packet, const bool is_video )
    : stream_id( enforce_stream_id( is_video, packet.at( 3 ) ) ),
      payload_start( packet.at( 8 ) + 9 ),
      PES_packet_length( (packet.at( 4 ) << 8) | packet.at( 5 ) ),
      data_alignment_indicator( packet.at( 6 ) & 0x04 ),
      PTS_DTS_flags( (packet.at( 7 ) & 0xc0) >> 6 ),
      presentation_time_stamp(),
      decoding_time_stamp()
  {
    if ( packet.at( 0 ) != 0
         or packet.at( 1 ) != 0
         or packet.at( 2 ) != 1 ) {
      throw InvalidMPEG( "invalid PES start code" );
    }

    if ( payload_start > packet.length() ) {
      throw InvalidMPEG( "invalid PES packet" );
    }

    /*
      unfortunately NBC San Francisco does not seem to use this
    if ( not data_alignment_indicator ) {
      throw runtime_error( "unaligned PES packet" );
    }
    */

    switch ( PTS_DTS_flags ) {
    case 0:
      throw UnsupportedMPEG( "missing PTS and DTS" );
    case 1:
      throw InvalidMPEG( "forbidden value of PTS_DTS_flags" );
    case 3:
      decoding_time_stamp = (((uint64_t(uint8_t(packet.at( 14 )) & 0x0F) >> 1) << 30) |
                             (uint8_t(packet.at( 15 )) << 22) |
                             ((uint8_t(packet.at( 16 )) >> 1) << 15) |
                             (uint8_t(packet.at( 17 )) << 7) |
                             (uint8_t(packet.at( 18 )) >> 1));

      if ( (packet.at( 14 ) & 0xf0) >> 4 != 1 ) {
        throw InvalidMPEG( "invalid DTS prefix bits" );
      }

      if ( (packet.at( 14 ) & 0x01) != 1 ) {
        throw InvalidMPEG( "invalid marker bit" );
      }

      if ( (packet.at( 16 ) & 0x01) != 1 ) {
        throw InvalidMPEG( "invalid marker bit" );
      }

      if ( (packet.at( 18 ) & 0x01) != 1 ) {
        throw InvalidMPEG( "invalid marker bit" );
      }

      /* fallthrough */

    case 2:
      presentation_time_stamp = (((uint64_t(uint8_t(packet.at( 9 )) & 0x0F) >> 1) << 30) |
                                 (uint8_t(packet.at( 10 )) << 22) |
                                 ((uint8_t(packet.at( 11 )) >> 1) << 15) |
                                 (uint8_t(packet.at( 12 )) << 7) |
                                 (uint8_t(packet.at( 13 )) >> 1));

      if ( (packet.at( 9 ) & 0xf0) >> 4 != PTS_DTS_flags ) {
        throw InvalidMPEG( "invalid PTS prefix bits" );
      }

      if ( (packet.at( 9 ) & 0x01) != 1 ) {
        throw InvalidMPEG( "invalid marker bit" );
      }

      if ( (packet.at( 11 ) & 0x01) != 1 ) {
        throw InvalidMPEG( "invalid marker bit" );
      }

      if ( (packet.at( 13 ) & 0x01) != 1 ) {
         throw InvalidMPEG( "invalid marker bit" );
      }
    }

    if ( PTS_DTS_flags == 2 ) {
      decoding_time_stamp = presentation_time_stamp;
    }

    //    cerr << "PES packet header, is_video=" << is_video << ", dts_27M = " << 300 * decoding_time_stamp << ", pts_27M = " << 300 * presentation_time_stamp << "\n";
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
      throw UnsupportedMPEG( "unsupported format: " + format );
    }
  }
};

struct AudioBlock
{
  uint64_t presentation_time_stamp;
  int16_t left[ audio_samples_per_block ], right[ audio_samples_per_block ];
};

class A52AudioDecoder
{
private:
  struct A52Deleter
  {
    void operator()( a52_state_t * const x ) const
    {
      a52_free( x );
    }
  };

  unique_ptr<a52_state_t, A52Deleter> decoder_;

  sample_t check_sample( const sample_t & sample )
  {
    if ( sample > 32767.4 or sample < -32767.4 ) {
      throw InvalidMPEG( "sample out of range" );
    }

    return sample;
  }

public:
  A52AudioDecoder()
    : decoder_( notnull( "a52_init", a52_init( MM_ACCEL_DJBFFT ) ) )
  {}

  void decode_frames( TimestampedPESPacket & PES_packet /* mutable */,
                      queue<AudioBlock> & decoded_samples )
  {
    while ( PES_packet.payload_length() ) {
      if ( PES_packet.payload_length() < 7 ) {
        throw InvalidMPEG( "PES packet too small" );
      }

      // cerr << "Audio frame with pts_27M = " << 300 * PES_packet.presentation_time_stamp << "\n";

      int flags, sample_rate, bit_rate;
      const int frame_length = a52_syncinfo( PES_packet.payload_start(),
                                             &flags, &sample_rate, &bit_rate );
      if ( frame_length == 0 ) {
        throw InvalidMPEG( "invalid A/52 frame" );
      }

      if ( sample_rate != atsc_audio_sample_rate ) {
        throw UnsupportedMPEG( "unsupported sample_rate of " + to_string( sample_rate ) + " Hz" );
      }

      flags = A52_STEREO | A52_ADJUST_LEVEL;
      sample_t level = 32767;

      if ( a52_frame( decoder_.get(), PES_packet.payload_start(),
                      &flags, &level, 0 ) ) {
        throw InvalidMPEG( "a52_frame returned error" );
      }

      if ( flags != A52_STEREO and flags != A52_DOLBY ) {
        throw InvalidMPEG( "could not downmix to stereo: flags = " + to_string( flags ) );
      }

      /* decode all six blocks in the frame */
      for ( unsigned int block_id = 0; block_id < 6; block_id++ ) {
        if ( a52_block( decoder_.get() ) ) {
          throw InvalidMPEG( "a52_block returned error" );
        }

        AudioBlock chunk;
        chunk.presentation_time_stamp = 300 * PES_packet.presentation_time_stamp + block_id * audio_block_duration;
        //        cerr << "Making audio block with pts_27M = " << chunk.presentation_time_stamp << "\n";

        sample_t * next_sample = a52_samples( decoder_.get() );

        for ( unsigned int i = 0; i < audio_samples_per_block; i++ ) {
          chunk.left[ i ] = static_cast<int16_t>( lround( check_sample( *next_sample ) ) );
          next_sample++;
        }

        for ( unsigned int i = 0; i < audio_samples_per_block; i++ ) {
          chunk.right[ i ] = static_cast<int16_t>( lround( check_sample( *next_sample ) ) );
          next_sample++;
        }

        decoded_samples.push( chunk );
      }

      /* get ready for next frame */
      PES_packet.payload_start_index += frame_length;
      PES_packet.presentation_time_stamp += 6 * audio_block_duration / 300;
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
      throw InvalidMPEG( "sequence not flagged as MPEG-2 part 2 video" );
    }

    if ( (sequence->flags & SEQ_FLAG_PROGRESSIVE_SEQUENCE) != (progressive_sequence_ ? SEQ_FLAG_PROGRESSIVE_SEQUENCE : 0) ) {
      throw StreamMismatch( "progressive/interlaced sequence mismatch" );
    }

    if ( sequence->width != physical_luma_width() ) {
      throw StreamMismatch( "width mismatch" );
    }

    if ( sequence->height != physical_luma_height() ) {
      throw StreamMismatch( "height mismatch" );
    }

    if ( sequence->chroma_width != physical_luma_width() / 2 ) {
      throw StreamMismatch( "chroma width mismatch" );
    }

    if ( sequence->chroma_height != physical_luma_height() / 2 ) {
      throw StreamMismatch( "chroma height mismatch" );
    }

    if ( sequence->picture_width != display_width_ ) {
      throw StreamMismatch( "picture width mismatch" );
    }

    if ( sequence->picture_height != display_height_ ) {
      throw StreamMismatch( "picture height mismatch" );
    }

    if ( sequence->display_width != display_width_ ) {
      throw StreamMismatch( "display width mismatch" );
    }

    if ( sequence->display_height != display_height_ ) {
      throw StreamMismatch( "display height mismatch" );
    }

    if ( sequence->pixel_width != 1
         or sequence->pixel_height != 1 ) {
      throw UnsupportedMPEG( "non-square pels" );
    }

    if ( sequence->frame_period != frame_interval_ ) {
      throw StreamMismatch( "frame interval mismatch" );
    }
  }

  void output_picture( const mpeg2_picture_t * pic,
                       const mpeg2_fbuf_t * display_raster,
                       queue<VideoField> & output )
  {
    if ( not (pic->flags & PIC_FLAG_TAGS) ) {
      throw UnsupportedMPEG( "picture without timestamp" );
    }

    if ( progressive_sequence_ ) {
      if ( pic->nb_fields % 2 != 0 ) {
        throw runtime_error( "progressive sequence, but picture has odd number of fields" );
      }
    }

    bool next_field_is_top = (pic->flags & PIC_FLAG_TOP_FIELD_FIRST) | progressive_sequence_;
    uint64_t presentation_time_stamp_27M = 300 * ( (uint64_t( pic->tag ) << 32) | (pic->tag2) );

    /* output each field */
    for ( unsigned int field = 0; field < pic->nb_fields; field++ ) {
      output.emplace( presentation_time_stamp_27M,
                      next_field_is_top,
                      display_width_,
                      display_height_,
                      physical_luma_width(),
                      display_raster );

      next_field_is_top = !next_field_is_top;
      presentation_time_stamp_27M += frame_interval_ / 2; /* treat all as interlaced */
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
          throw UnsupportedMPEG( "PES packet with multiple pictures" );
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
        throw InvalidMPEG( "libmpeg2 is in STATE_INVALID" );
        break;
      case STATE_PICTURE:
        break;
      case STATE_PICTURE_2ND:
        throw UnsupportedMPEG( "unsupported field pictures" );
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
  bool is_video_; /* true = video, false = audio */

  string PES_packet_ {};

  void append_payload( const string_view & packet, const TSPacketHeader & header )
  {
    const string_view payload = packet.substr( header.payload_start );
    PES_packet_.append( payload.begin(), payload.end() );
  }

public:
  TSParser( const unsigned int pid, const bool is_video )
    : pid_( pid ),
      is_video_( is_video )
  {
    if ( pid >= (1 << 13) ) {
      throw runtime_error( "program ID must be less than " + to_string( 1 << 13 ) );
    }
  }

  void parse( const string_view & packet, queue<TimestampedPESPacket> & PES_packets )
  {
    TSPacketHeader header { packet };

    if ( header.pid != pid_ ) {
      return;
    }

    if ( header.payload_unit_start_indicator ) {
      /* start of new PES packet */

      /* step 1: parse and decode old PES packet if there is one */
      if ( not PES_packet_.empty() ) {
        /* make sure PES_packet_ is cleared even if header parsers subsequently throw an exception */
        string PES_packet = move( PES_packet_ );
        PES_packet_.clear();

        /* now, attempt to parse the accumulated payload as a PES packet */
        PESPacketHeader pes_header { PES_packet, is_video_ };

        PES_packets.emplace( pes_header.presentation_time_stamp,
                             pes_header.payload_start,
                             pes_header.PES_packet_length,
                             move( PES_packet ) );
      }

      /* step 2: start a new PES packet */
      append_payload( packet, header );
    } else if ( not PES_packet_.empty() ) {
      /* interior TS packet within a PES packet */
      append_payload( packet, header );
    }
  }
};

class Y4M_Writer
{
private:
  bool next_field_is_top_ { true };

  uint64_t wallclock_time_for_outer_timestamp_zero_;
  uint64_t pending_chunk_outer_timestamp_ {};
  unsigned int pending_chunk_index_ {};
  vector<Raster> pending_chunk_;
  unsigned int filler_field_count_ {};

  unsigned int frame_interval_;

  string directory_;
  string y4m_header_;

  uint64_t outer_timestamp_ {};

  optional<int64_t> last_offset_ {};

  Raster & pending_frame()
  {
    return pending_chunk_.at( pending_chunk_index_ );
  }

  void write_frame_to_disk( const uint64_t first_field_presentation_time_stamp )
  {
    if ( pending_chunk_index_ == 0 ) {
      pending_chunk_outer_timestamp_ = outer_timestamp_ / 300;
      cerr << "Starting new video chunk with outer timestamp = " << pending_chunk_outer_timestamp_ << ", with ";
      cerr << wallclock_ms_until_next_chunk_is_due() << " ms until this chunk is due.\n";
    }

    if ( pending_chunk_index_ == pending_chunk_.size() - 1 ) {
      const string filename = to_string( pending_chunk_outer_timestamp_ ) + ".y4m";
      const string info_filename = to_string( pending_chunk_outer_timestamp_ ) + ".y4m.info";

      /* output to tmp_dir first if tmp_dir is not empty */
      string output_dir = tmp_dir.empty() ? directory_ : tmp_dir;

      cerr << "Writing " << output_dir + "/" + filename << " ... ";
      cerr << "(due in " << wallclock_ms_until_next_chunk_is_due() << " ms) ";

      FileDescriptor directory_fd_ { CheckSystemCall( "open " + output_dir, open( output_dir.c_str(),
                                                                                  O_DIRECTORY ) ) };

      FileDescriptor output_ { CheckSystemCall( "openat", openat( directory_fd_.fd_num(),
                                                                  filename.c_str(),
                                                                  O_WRONLY | O_CREAT | O_EXCL,
                                                                  S_IRUSR | S_IWUSR ) ) };

      output_.write( y4m_header_ );

      for ( const auto & pending_frame : pending_chunk_ ) {
        output_.write( "FRAME\n" );

        /* Y */
        output_.write( string_view { reinterpret_cast<char *>( pending_frame.Y.get() ), pending_frame.width * pending_frame.height } );

        /* Cb */
        output_.write( string_view { reinterpret_cast<char *>( pending_frame.Cb.get() ), (pending_frame.width/2) * (pending_frame.height/2) } );

        /* Cr */
        output_.write( string_view { reinterpret_cast<char *>( pending_frame.Cr.get() ), (pending_frame.width/2) * (pending_frame.height/2) } );
      }

      output_.close(); /* make sure output is flushed before renaming */

      /* move output file if tmp_dir is not empty */
      if ( output_dir != directory_ ) {
        fs::rename( fs::path( output_dir ) / filename,
                    fs::path( directory_ ) / filename );
      }

      cerr << "done.\n";

      /* write diagnostic output */
      FileDescriptor info_ { CheckSystemCall( "openat", openat( directory_fd_.fd_num(),
                                                                info_filename.c_str(),
                                                                O_WRONLY | O_CREAT | O_EXCL,
                                                                S_IRUSR | S_IWUSR ) ) };

      string info_string = /* wallclock timestamp */ to_string( timestamp_ms() ) + " "
        + /* video timestamp */ to_string( pending_chunk_outer_timestamp_ ) + " "
        + /* due in (ms) */ to_string( wallclock_ms_until_next_chunk_is_due() ) + " "
        + /* filler fields */ to_string( filler_field_count_ );

      info_.write( info_string + "\n");

      info_.close();

      if ( output_dir != directory_ ) {
        fs::rename( fs::path( output_dir ) / info_filename,
                    fs::path( directory_ ) / info_filename );
      }

      /* reset filler field count */
      filler_field_count_ = 0;

      /* if we wrote the chunk out early, consumers might read it and depend on this new timebase */
      if ( wallclock_ms_until_next_chunk_is_due() > 0 ) {
        wallclock_time_for_outer_timestamp_zero_ -= wallclock_ms_until_next_chunk_is_due();
      }
    }

    /* advance virtual clock */
    last_offset_ = first_field_presentation_time_stamp - outer_timestamp_;
    outer_timestamp_ += frame_interval_;
    pending_chunk_index_ = (pending_chunk_index_ + 1) % pending_chunk_.size();
  }

public:
  Y4M_Writer( const uint64_t initial_wallclock_timestamp,
              const string directory,
              const unsigned int frames_per_chunk,
              const VideoParameters & params )
    : wallclock_time_for_outer_timestamp_zero_( initial_wallclock_timestamp ),
      pending_chunk_(),
      frame_interval_( params.frame_interval ),
      directory_( directory ),
      y4m_header_( "YUV4MPEG2 W" + to_string( params.width )
                   + " H" + to_string( params.height ) + " " + params.y4m_description
                   + " A1:1 C420mpeg2\n" )
  {
    for ( unsigned int i = 0; i < frames_per_chunk; i++ ) {
      pending_chunk_.emplace_back( params.width, params.height );
    }
  }

  int wallclock_ms_until_next_chunk_is_due() const
  {
    const int next_chunk_is_due_wallclock_ms
      = pending_chunk_outer_timestamp_ / 90 + wallclock_time_for_outer_timestamp_zero_;
    return next_chunk_is_due_wallclock_ms - timestamp_ms();
  }

  void set_next_field_is_filler() { filler_field_count_++; }

  bool next_field_is_top() const { return next_field_is_top_; }

  void write_raw( const VideoField & field )
  {
    if ( field.top_field != next_field_is_top_ ) {
      throw runtime_error( "field cadence mismatch" );
    }

    /* copy field to proper lines of pending frame */

    /* copy Y */
    for ( unsigned int source_row = 0, dest_row = (next_field_is_top_ ? 0 : 1);
          source_row < field.contents->height;
          source_row += 1, dest_row += 2 ) {
      memcpy( pending_frame().Y.get() + dest_row * pending_frame().width,
              field.contents->Y.get() + source_row * pending_frame().width,
              pending_frame().width );
    }

    /* copy Cb */
    for ( unsigned int source_row = 0, dest_row = (next_field_is_top_ ? 0 : 1);
          source_row < field.contents->height/2;
          source_row += 1, dest_row += 2 ) {
      memcpy( pending_frame().Cb.get() + dest_row * pending_frame().width/2,
              field.contents->Cb.get() + source_row * pending_frame().width/2,
              pending_frame().width/2 );
    }

    /* copy Cr */
    for ( unsigned int source_row = 0, dest_row = (next_field_is_top_ ? 0 : 1);
          source_row < field.contents->height/2;
          source_row += 1, dest_row += 2 ) {
      memcpy( pending_frame().Cr.get() + dest_row * pending_frame().width/2,
              field.contents->Cr.get() + source_row * pending_frame().width/2,
              pending_frame().width/2 );
    }

    next_field_is_top_ = !next_field_is_top_;

    if ( next_field_is_top_ ) {
      /* print out frame */
      write_frame_to_disk( field.presentation_time_stamp - frame_interval_ / 2 );
    }
  }

  /* get last outer-inner timestamp offset (for verifying a/v sync) */
  optional<int64_t> last_offset() const { return last_offset_; }

  /* need to reset tracking after a resync */
  void reset_sync_tracking() { last_offset_.reset(); }

  uint64_t outer_timestamp() const { return outer_timestamp_; }
};

class VideoOutput
{
private:
  unsigned int frame_interval_;
  uint64_t expected_inner_timestamp_;

  VideoField missing_field_;

  void write_single_field( const VideoField & field, Y4M_Writer & writer )
  {
    writer.write_raw( field );
    expected_inner_timestamp_ += frame_interval_ / 2;
  }

public:
  VideoOutput( const VideoParameters & params, const uint64_t initial_inner_timestamp )
    : frame_interval_( params.frame_interval ),
      expected_inner_timestamp_( initial_inner_timestamp ),
      missing_field_( 0, false, params.width, params.height )
  {
    cerr << "VideoOutput: constructed with initial timestamp = " << initial_inner_timestamp << "\n";
  }

  void write( const VideoField & field, Y4M_Writer & writer )
  {
    /*
    cerr << "New field, expected ts = " << expected_inner_timestamp_
         << ", got " << field.presentation_time_stamp << " -> timestamp_difference = "
         << timestamp_difference( expected_inner_timestamp_, field.presentation_time_stamp ) << "\n";
    */

    const int64_t diff = timestamp_difference( expected_inner_timestamp_,
                                               field.presentation_time_stamp );

    /* gap is too big -> bomb out and force reinitialization */
    if ( abs( diff ) > frame_interval_ * 60 * 6 ) {
      throw HugeTimestampDifference( "huge video timestamp difference, expected = " + to_string( expected_inner_timestamp_ ) + " vs. actual = " + to_string( field.presentation_time_stamp )  );
    }

    /* field's moment has passed -> ignore */
    if ( diff > 0 ) {
      cerr << "Warning, ignoring field whose timestamp has already passed (diff = " << diff / double( frame_interval_ ) << " frames).\n";
      return;
    }

    /* field's moment is in the future -> insert filler fields */
    while ( timestamp_difference( expected_inner_timestamp_,
                                  field.presentation_time_stamp )
            < -9 * int64_t( frame_interval_ ) / 8 ) {
      cerr << "Generating replacement fields to fill in gap (diff now "
           << timestamp_difference( expected_inner_timestamp_,
                                    field.presentation_time_stamp ) / double( frame_interval_ ) << " frames)\n";
      write_filler_field( writer );
      write_filler_field( writer );
    }

    /* write the originally requested field */
    if ( field.top_field == writer.next_field_is_top() ) {
      write_single_field( field, writer );
    } else {
      cerr << "ignoring field with mismatched cadence\n";
    }
  }

  void write_filler_field( Y4M_Writer & writer )
  {
    missing_field_.presentation_time_stamp = expected_inner_timestamp_;
    missing_field_.top_field = writer.next_field_is_top();
    writer.set_next_field_is_filler();
    write_single_field( missing_field_, writer );
  }
};

class WavWriter
{
private:
  uint64_t wallclock_time_for_outer_timestamp_zero_;
  uint64_t pending_chunk_outer_timestamp_ {};
  unsigned int pending_chunk_index_ {};
  vector<AudioBlock> pending_chunk_;
  string overlap_samples_;

  string directory_;
  string wav_header_;

  uint64_t outer_timestamp_ {};

  optional<int64_t> last_offset_ {};

public:
  WavWriter( const uint64_t initial_wallclock_timestamp,
             const string directory,
             const unsigned int audio_blocks_per_chunk,
             const unsigned int audio_sample_overlap )
    : wallclock_time_for_outer_timestamp_zero_( initial_wallclock_timestamp ),
      pending_chunk_(),
      overlap_samples_( audio_sample_overlap * 2 * 2, 0 ),
      directory_( directory ),
      wav_header_()
  {
    for ( unsigned int i = 0; i < audio_blocks_per_chunk; i++ ) {
      pending_chunk_.emplace_back();
    }

    wav_header_ += "RIFF";
    const uint32_t ChunkSize = htole32( overlap_samples_.size() + audio_blocks_per_chunk * audio_samples_per_block * 2 * 2 + 36 );
    wav_header_ += string( reinterpret_cast<const char *>( &ChunkSize ), sizeof( ChunkSize ) );
    wav_header_ += "WAVE";

    wav_header_ += "fmt ";
    const uint32_t SubChunk1Size = htole32( 16 );
    wav_header_ += string( reinterpret_cast<const char *>( &SubChunk1Size ), sizeof( SubChunk1Size ) );
    wav_header_ += '\001'; /* PCM */
    wav_header_ += '\000';

    wav_header_ += '\002'; /* 2-channel */
    wav_header_ += '\000';

    const uint32_t SampleRate = htole32( atsc_audio_sample_rate );
    wav_header_ += string( reinterpret_cast<const char *>( &SampleRate ), sizeof( SampleRate ) );

    const uint32_t ByteRate = htole32( atsc_audio_sample_rate * 2 * 2 );
    wav_header_ += string( reinterpret_cast<const char *>( &ByteRate ), sizeof( ByteRate ) );

    wav_header_ += '\004'; /* BlockAlign */
    wav_header_ += '\000';

    wav_header_ += '\020'; /* 16 bits per sample */
    wav_header_ += '\000';

    wav_header_ += "data";

    const uint32_t SubChunk2Size = htole32( overlap_samples_.size() + audio_blocks_per_chunk * audio_samples_per_block * 2 * 2 );
    wav_header_ += string( reinterpret_cast<const char *>( &SubChunk2Size ), sizeof( SubChunk2Size ) );
  }

  int wallclock_ms_until_next_chunk_is_due() const
  {
    const int next_chunk_is_due_wallclock_ms
      = pending_chunk_outer_timestamp_ / 90 + wallclock_time_for_outer_timestamp_zero_;
    return next_chunk_is_due_wallclock_ms - timestamp_ms();
  }

  void write_raw( const AudioBlock & audio_block )
  {
    pending_chunk_.at( pending_chunk_index_ ) = audio_block;

    if ( pending_chunk_index_ == 0 ) {
      pending_chunk_outer_timestamp_ = outer_timestamp_ / 300;
      cerr << "Starting new audio chunk with outer timestamp = " << pending_chunk_outer_timestamp_ << ", with ";
      cerr << wallclock_ms_until_next_chunk_is_due() << " ms until this chunk is due.\n";
    }

    if ( pending_chunk_index_ == pending_chunk_.size() - 1 ) {
      const string filename = to_string( pending_chunk_outer_timestamp_ ) + ".wav";

      /* output to tmp_dir first if tmp_dir is not empty */
      string output_dir = tmp_dir.empty() ? directory_ : tmp_dir;

      cerr << "Writing " << output_dir + "/" + filename << " ... ";
      cerr << "(due in " << wallclock_ms_until_next_chunk_is_due() << " ms) ";

      FileDescriptor directory_fd_ { CheckSystemCall( "open " + output_dir, open( output_dir.c_str(),
                                                                                  O_DIRECTORY ) ) };

      FileDescriptor output_ { CheckSystemCall( "openat", openat( directory_fd_.fd_num(),
                                                                  filename.c_str(),
                                                                  O_WRONLY | O_CREAT | O_EXCL,
                                                                  S_IRUSR | S_IWUSR ) ) };

      output_.write( wav_header_ );

      /* write the overlap (last 648 samples of last chunk) first */
      output_.write( overlap_samples_ );

      /* now write the new samples */
      string serialized_samples;

      for ( const auto & pending_block : pending_chunk_ ) {
        for ( unsigned int sample_id = 0; sample_id < audio_samples_per_block; sample_id++ ) {
          serialized_samples += string_view( reinterpret_cast<const char *>( pending_block.left + sample_id ),
                                             sizeof( int16_t ) );
          serialized_samples += string_view( reinterpret_cast<const char *>( pending_block.right + sample_id ),
                                             sizeof( int16_t ) );
        }
      }


      output_.write( serialized_samples );

      /* now record the last samples for next time's overlap */
      if ( serialized_samples.size() < overlap_samples_.size() ) {
        throw runtime_error( "not enough overlap samples" );
      }

      overlap_samples_ = serialized_samples.substr( serialized_samples.size() - overlap_samples_.size() );

      /* Bug check */
      if ( overlap_samples_.size() != opus_sample_overlap * 2 * 2 ) {
        throw runtime_error( "BUG: overlap_samples is wrong size" );
      }

      output_.close(); /* make sure output is flushed before renaming */

      /* move output file if tmp_dir is not empty */
      if ( output_dir != directory_ ) {
        fs::rename( fs::path( output_dir ) / filename,
                    fs::path( directory_ ) / filename );
      }

      cerr << "done.\n";

      /* if we wrote the chunk out early, consumers might read it and depend on this new timebase */
      if ( wallclock_ms_until_next_chunk_is_due() > 0 ) {
        wallclock_time_for_outer_timestamp_zero_ -= wallclock_ms_until_next_chunk_is_due();
      }
    }

    /* advance virtual clock */
    last_offset_ = audio_block.presentation_time_stamp - outer_timestamp_;
    outer_timestamp_ += audio_block_duration;
    pending_chunk_index_ = (pending_chunk_index_ + 1) % pending_chunk_.size();
  }

  /* get last outer-inner timestamp offset (for verifying a/v sync) */
  optional<int64_t> last_offset() const { return last_offset_; }

  /* need to reset tracking after a resync */
  void reset_sync_tracking() { last_offset_.reset(); }

  uint64_t outer_timestamp() const { return outer_timestamp_; }
};

class AudioOutput
{
private:
  uint64_t expected_inner_timestamp_;
  AudioBlock silence_ {};

  void write_block( const AudioBlock & audio_block, WavWriter & writer )
  {
    writer.write_raw( audio_block );
    expected_inner_timestamp_ += audio_block_duration;
  }

public:
  AudioOutput( const uint64_t initial_inner_timestamp )
    : expected_inner_timestamp_( initial_inner_timestamp )
  {
    cerr << "AudioOutput: constructed with initial timestamp = " << initial_inner_timestamp << "\n";
  }

  void write( const AudioBlock & audio_block, WavWriter & writer )
  {

    /*
    cerr << "New audio block, expected ts = " << expected_inner_timestamp_
         << ", got " << audio_block.presentation_time_stamp << " -> timestamp_difference = "
         << timestamp_difference( expected_inner_timestamp_, audio_block.presentation_time_stamp ) << "\n";
    */

    const int64_t diff = timestamp_difference( expected_inner_timestamp_,
                                               audio_block.presentation_time_stamp );

    /* gap is too big -> bomb out and force reinitialization */
    if ( abs( diff ) > audio_block_duration * 187 * 6 ) {
      throw HugeTimestampDifference( "huge audio timestamp difference, expected = " + to_string( expected_inner_timestamp_ ) + " vs. actual = " + to_string( audio_block.presentation_time_stamp )  );
    }

    /* block's moment has passed -> ignore */
    if ( diff > 0 ) {
      cerr << "Warning, ignoring audio whose timestamp has already passed (diff = " << diff / double( audio_block_duration ) << " blocks).\n";
      return;
    }

    /* block's moment is in the future -> insert silence */
    while ( timestamp_difference( expected_inner_timestamp_,
                                  audio_block.presentation_time_stamp )
            < -9 * int64_t( audio_block_duration ) / 8 ) {
      cerr << "Generating silent blocks to fill in gap (diff now "
           << timestamp_difference( expected_inner_timestamp_,
                                    audio_block.presentation_time_stamp ) / double( audio_block_duration ) << " blocks)\n";
      write_silence( writer );
    }

    write_block( audio_block, writer );
  }

  void write_silence( WavWriter & writer )
  {
    silence_.presentation_time_stamp = expected_inner_timestamp_;
    write_block( silence_, writer );
  }
};

class AudioVideoDecoder
{
  TSParser video_parser;
  TSParser audio_parser;
  queue<TimestampedPESPacket> video_PES_packets {}; /* output of TSParser */
  queue<TimestampedPESPacket> audio_PES_packets {}; /* output of TSParser */

  VideoParameters params;

  MPEG2VideoDecoder video_decoder { params };
  queue<VideoField> decoded_fields {}; /* output of MPEG2VideoDecoder */
  Y4M_Writer y4m_writer;

  A52AudioDecoder audio_decoder {};
  queue<AudioBlock> decoded_samples {}; /* output of A52AudioDecoder */
  WavWriter wav_writer;

  bool outputs_initialized = false;
  optional<VideoOutput> video_output {};
  optional<AudioOutput> audio_output {};

  string input_buffer {};

  void resync()
  {
    /* synchronize the outputs before the resync */

    /* step 0: advance video and audio to "catch up" to real wallclock time */
    while ( y4m_writer.wallclock_ms_until_next_chunk_is_due() < 0 ) {
      video_output->write_filler_field( y4m_writer );
    }

    while ( wav_writer.wallclock_ms_until_next_chunk_is_due() < 0 ) {
      audio_output->write_silence( wav_writer );
    }

    /* first: advance video to go just beyond audio */
    while ( y4m_writer.outer_timestamp() < wav_writer.outer_timestamp() ) {
      video_output->write_filler_field( y4m_writer );
    }

    /* step 1.5: make sure we end on a bottom field */
    if ( not y4m_writer.next_field_is_top() ) {
      video_output->write_filler_field( y4m_writer );
    }

    /* second: advance audio to go just beyond video */
    while ( wav_writer.outer_timestamp() < y4m_writer.outer_timestamp() ) {
      audio_output->write_silence( wav_writer );
    }

    /* because the audio blocks are shorter than video fields, this will get us the closest sync */

    /* next: reset everything and let it resync on next video field */
    video_output.reset();
    audio_output.reset();
    y4m_writer.reset_sync_tracking();
    wav_writer.reset_sync_tracking();
    outputs_initialized = false;
  }

public:
  AudioVideoDecoder( const unsigned int video_pid,
                     const unsigned int audio_pid,
                     const VideoParameters & params,
                     const unsigned int frames_per_chunk,
                     const unsigned int audio_blocks_per_chunk,
                     const unsigned int audio_sample_overlap,
                     const string & video_directory,
                     const string & audio_directory,
                     const uint64_t initial_wallclock_timestamp )
    : video_parser( video_pid, true ),
      audio_parser( audio_pid, false ),
      params( params ),
      y4m_writer( initial_wallclock_timestamp, video_directory, frames_per_chunk, params ),
      wav_writer( initial_wallclock_timestamp, audio_directory, audio_blocks_per_chunk, audio_sample_overlap )
  {}

  void parse_input( const string & new_chunk )
  {
    /* parse transport stream packets into video and audio PES packets */
    input_buffer.append( new_chunk );

    if ( input_buffer.size() < ts_packet_length ) {
      return;
    }

    const unsigned int packets_in_chunk = input_buffer.size() / ts_packet_length;
    const string chunk = input_buffer.substr( 0, packets_in_chunk * ts_packet_length );
    input_buffer.erase( 0, packets_in_chunk * ts_packet_length );
    const string_view chunk_view { chunk };

    for ( unsigned packet_no = 0; packet_no < packets_in_chunk; packet_no++ ) {
      try {
        video_parser.parse( chunk_view.substr( packet_no * ts_packet_length,
                                               ts_packet_length ),
                            video_PES_packets );
        audio_parser.parse( chunk_view.substr( packet_no * ts_packet_length,
                                               ts_packet_length ),
                            audio_PES_packets );
      } catch ( const non_fatal_exception & e ) {
        print_exception( "transport stream input", e );
      }
    }
  }

  void decode_video()
  {
    while ( not video_PES_packets.empty() ) {
      try {
        TimestampedPESPacket PES_packet { move( video_PES_packets.front() ) };
        video_PES_packets.pop();
        video_decoder.decode_frame( PES_packet, decoded_fields );
      } catch ( const non_fatal_exception & e ) {
        print_exception( "video decode", e );
        video_decoder = MPEG2VideoDecoder( params );
      }
    }
  }

  void decode_audio()
  {
    while ( not audio_PES_packets.empty() ) {
      try {
        TimestampedPESPacket PES_packet { move( audio_PES_packets.front() ) };
        audio_PES_packets.pop();
        audio_decoder.decode_frames( PES_packet, decoded_samples );
      } catch ( const non_fatal_exception & e ) {
        print_exception( "audio decode", e );
        audio_decoder = A52AudioDecoder();
      }
    }
  }

  void output_video()
  {
    while ( not decoded_fields.empty() ) {
      /* initialize audio and video outputs with earliest video field as first timestamp */
      if ( not outputs_initialized ) {
        if ( decoded_fields.front().top_field != y4m_writer.next_field_is_top() ) {
          decoded_fields.pop();
          continue;
        }
        video_output.emplace( params, decoded_fields.front().presentation_time_stamp );
        audio_output.emplace( decoded_fields.front().presentation_time_stamp );
        decoded_samples = {}; /* don't confuse newly resynced audio output with old audio samples
                                 (which may be old enough, relative to the new video frame, to
                                 cause a HugeTimestampDifference exception) */
        outputs_initialized = true;
      }

      try {
        video_output.value().write( decoded_fields.front(), y4m_writer );
      } catch ( const HugeTimestampDifference & e ) {
        /* need to reinitialize inner timestamps */
        print_exception( "video output", e );
        resync();
      }
      decoded_fields.pop();
    }
  }

  void output_audio()
  {
    while ( not decoded_samples.empty() ) {
      /* only initialize timestamps on valid video */
      if ( not outputs_initialized ) {
        break;
      }

      try {
        audio_output.value().write( decoded_samples.front(), wav_writer );
      } catch ( const HugeTimestampDifference & e ) {
        /* need to reinitialize inner timestamps */
        print_exception( "audio output", e );
        resync();
      }
      decoded_samples.pop();
    }
  }

  void check_av_sync() const
  {
    /* check a/v sync */
    if ( y4m_writer.last_offset() and wav_writer.last_offset() ) {
      uint64_t diff = abs( *y4m_writer.last_offset() - *wav_writer.last_offset() );
      if ( diff > 1175513 /* 43.5 ms */ ) {
        /* this value is the maximum video slop allowed for 1080i30 video (900900 * 9 / 8 = 1013512.5)
           plus the maximum audio slop allowed for 48Hz A/52 (144000 * 9 / 8 = 162000),
           to get a worst-case offset */
        cerr << "Warning: a/v sync is off by " << (diff / 27000.0) << " ms\n";
        throw runtime_error( "BUG: a/v sync failure" );
      }
    } else {
      cerr << "Warning: a/v sync check temporarily disabled\n";
    }
  }

  /* don't let audio or video get more than N seconds behind wallclock time */
  void enforce_wallclock_lag_limit()
  {
    if ( y4m_writer.wallclock_ms_until_next_chunk_is_due() < -3500 ) {
      cerr << "Video lags by " << -y4m_writer.wallclock_ms_until_next_chunk_is_due() << " ms, resyncing.\n";
      resync();
    }

    if ( wav_writer.wallclock_ms_until_next_chunk_is_due() < -6300 ) {
      cerr << "Audio lags by " << -wav_writer.wallclock_ms_until_next_chunk_is_due() << " ms, resyncing.\n";
      resync();
    }
  }
};

int main( int argc, char *argv[] )
{
  try {
    if ( argc < 1 ) { /* for pedants */
      abort();
    }

    string tcp_addr;

    const option cmd_line_opts[] = {
      { "tmp",    required_argument, nullptr, 't' },
      { "tcp",    required_argument, nullptr, 'c' },
      { nullptr,  0,                 nullptr,  0  }
    };

    while ( true ) {
      const int opt = getopt_long( argc, argv, "t:c:", cmd_line_opts, nullptr );
      if ( opt == -1 ) {
        break;
      }

      switch ( opt ) {
      case 't':
        tmp_dir = optarg;
        break;
      case 'c':
        tcp_addr = optarg;
        break;
      default:
        print_usage( argv[0] );
        return EXIT_FAILURE;
      }
    }

    if ( optind != argc - 8 ) {
      print_usage( argv[0] );
      return EXIT_FAILURE;
    }

    /* NB: "1080i30" is the preferred notation in Poynton's books and "Video Demystified" */
    const unsigned int video_pid = stoi( argv[ optind++ ], nullptr, 0 );
    const unsigned int audio_pid = stoi( argv[ optind++ ], nullptr, 0 );
    const VideoParameters params { argv[ optind++ ] };
    const unsigned int frames_per_chunk = stoi( argv[ optind++ ] );
    const unsigned int audio_blocks_per_chunk = stoi( argv[ optind++ ] );
    const unsigned int audio_sample_overlap = stoi( argv[ optind++ ] );
    const string video_directory = argv[ optind++ ];
    const string audio_directory = argv[ optind++ ];

    if ( audio_sample_overlap != opus_sample_overlap ) {
      throw runtime_error( "audio_sample_overlap must be " + to_string( opus_sample_overlap ) );
    }

    shared_ptr<FileDescriptor> input;
    if ( tcp_addr.empty() ) {
      /* read from stdin if a remote address is not provided */
      input = make_shared<FileDescriptor>( STDIN_FILENO );
    } else {
      auto idx = tcp_addr.find( ':' );
      string ip = tcp_addr.substr( 0, idx );
      uint16_t port = narrow_cast<uint16_t>( stoi( tcp_addr.substr( idx + 1 ) ) );

      input = make_shared<TCPSocket>();
      auto sock = dynamic_pointer_cast<TCPSocket>( input );
      sock->connect( { ip, port } );
      cerr << "Connected to " << tcp_addr << endl;
    }

    AudioVideoDecoder decoder { video_pid, audio_pid, params,
                                frames_per_chunk, audio_blocks_per_chunk,
                                audio_sample_overlap,
                                video_directory, audio_directory,
                                timestamp_ms() };

    Poller poller;
    poller.add_action( { *input, Direction::In,
                         [&decoder, &input] {
                           decoder.parse_input( input->read() );
                           decoder.decode_video();
                           decoder.decode_audio();
                           return ResultType::Continue;
                         } } );

    while ( true ) {
      const auto ret = poller.poll( 500 );
      if ( ret.result == Poller::Result::Type::Exit ) {
        return EXIT_SUCCESS;
      }

      decoder.output_video();
      decoder.output_audio();
      decoder.check_av_sync();
      decoder.enforce_wallclock_lag_limit();
    }
  } catch ( const exception & e ) {
    print_exception( argv[ 0 ], e );
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
