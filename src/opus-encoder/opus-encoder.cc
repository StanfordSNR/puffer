/* -*-mode:c++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */

#include <memory>
#include <iostream>
#include <vector>
#include <endian.h>

#include <sndfile.hh>
#include <opus/opus.h>

extern "C" {
#include <libavformat/avformat.h>
}

#include "media_formats.hh"

const unsigned int SAMPLE_RATE = 48000; /* Hz */
const unsigned int NUM_CHANNELS = 2;
const unsigned int NUM_SAMPLES_IN_OPUS_FRAME = 960;
const unsigned int EXPECTED_LOOKAHEAD = 312; /* 6.5 ms * 48 kHz */
const unsigned int EXTRA_FRAMES_PREPENDED = 10;
const unsigned int OVERLAP_SAMPLES_PREPENDED = (EXTRA_FRAMES_PREPENDED + 1) * NUM_SAMPLES_IN_OPUS_FRAME - EXPECTED_LOOKAHEAD;
const unsigned int NUM_SAMPLES_IN_OUTPUT = 230400; /* 48kHz * 4.8s */
const unsigned int NUM_SAMPLES_IN_INPUT = OVERLAP_SAMPLES_PREPENDED + NUM_SAMPLES_IN_OUTPUT;
const unsigned int MAX_COMPRESSED_FRAME_SIZE = 131072; /* bytes */
const unsigned int WEBM_TIMEBASE = 1000;

/* make sure target file length is integer number of Opus frames */
static_assert( (NUM_SAMPLES_IN_OUTPUT / NUM_SAMPLES_IN_OPUS_FRAME) * NUM_SAMPLES_IN_OPUS_FRAME
               == NUM_SAMPLES_IN_OUTPUT );

const unsigned int NUM_FRAMES_IN_OUTPUT = NUM_SAMPLES_IN_OUTPUT / NUM_SAMPLES_IN_OPUS_FRAME;

/* make sure file is long enough */
static_assert( NUM_SAMPLES_IN_OUTPUT > 4 * NUM_SAMPLES_IN_OPUS_FRAME );

/* view of raw input, and storage for compressed output */
using wav_frame_t = std::basic_string_view<int16_t>;
using opus_frame_t = std::pair<size_t, std::array<uint8_t, MAX_COMPRESSED_FRAME_SIZE>>; // length, buffer

/*

Theory of operation.

Starting with:

   chunk #0: samples 0      .. 230399 (4.8 s)
   chunk #1: samples 230400 .. 460799 (4.8 s)

Then prepend 648 samples of the previous chunk to each chunk:

   chunk #0: 648 silent       + 0      .. 230399 (4.8135 s)
   chunk #1: 229752 .. 230399 + 230400 .. 460799 (4.8135 s)

Now encode as Opus with first two frames independent:

 chunk #0:
   frame 0 (independent): 312 of ignore, then 648 silent                     (chop!)
   frame 1 (independent):                0      .. 959
   frame 2              :                960    .. 1919
   frame 3              :                1920   .. 2879
   ...
   frame 240            :                229400 .. 230399

 chunk #1:
   frame 0 (independent): 312 of ignore, then 229752 .. 230399                     (chop!)
   frame 1 (independent):                230400 .. 231359
   frame 2              :                231360 .. 232319
   frame 3              :                232320 .. 233279
   ...
   frame 240            :                459840 .. 460799

Chopping produces:

 chunk #0:
   frame 1 (independent):                0      .. 959
   frame 2              :                960    .. 1919
   frame 3              :                1920   .. 2879
   ...
   frame 240            :                229400 .. 230399

 chunk #1:
   frame 1 (independent):                230400 .. 231359
   frame 2              :                231360 .. 232319
   frame 3              :                232320 .. 233279
   ...
   frame 240            :                459840 .. 460799

So, for gapless playback, we prepend 648 samples from the previous chunk, then encode
the first two frames as independent (no prediction), and chop the
first of them. In practice we use 10 extra overlapping frames because this seems
to make all the audible glitches go away.

*/

using namespace std;

template <typename T>
inline T * notnull( const string & context, T * const x )
{
  return x ? x : throw runtime_error( context + ": returned null pointer" );
}

/* wrap Opus encoder in RAII class with error cherk */
class OpusEncoderWrapper
{
  struct opus_deleter { void operator()( OpusEncoder * x ) const { opus_encoder_destroy( x ); } };
  unique_ptr<OpusEncoder, opus_deleter> encoder_ {};

  static int opus_check( const int retval )
  {
    if ( retval < 0 ) {
      throw runtime_error( "Opus error: " + string( opus_strerror( retval ) ) );      
    }

    return retval;
  }

public:
  OpusEncoderWrapper( const int bit_rate )
  {
    int out;

    /* create encoder */
    encoder_.reset( notnull( "opus_encoder_create",
                             opus_encoder_create( SAMPLE_RATE, NUM_CHANNELS, OPUS_APPLICATION_AUDIO, &out ) ) );
    opus_check( out );

    /* set bit rate */
    opus_check( opus_encoder_ctl( encoder_.get(), OPUS_SET_BITRATE( bit_rate ) ) );

    /* check bitrate */
    opus_check( opus_encoder_ctl( encoder_.get(), OPUS_GET_BITRATE( &out ) ) );
    if ( out != bit_rate ) { throw runtime_error( "bit rate mismatch" ); }

    /* check sample rate */
    opus_check( opus_encoder_ctl( encoder_.get(), OPUS_GET_SAMPLE_RATE( &out ) ) );
    if ( out != SAMPLE_RATE ) { throw runtime_error( "sample rate mismatch" ); }

    /* check lookahead */
    opus_check( opus_encoder_ctl( encoder_.get(), OPUS_GET_LOOKAHEAD( &out ) ) );
    if ( out != EXPECTED_LOOKAHEAD ) { throw runtime_error( "lookahead mismatch" ); }
  }

  void disable_prediction()
  {
    opus_check( opus_encoder_ctl( encoder_.get(), OPUS_SET_PREDICTION_DISABLED( 1 ) ) );
  }

  void enable_prediction()
  {
    opus_check( opus_encoder_ctl( encoder_.get(), OPUS_SET_PREDICTION_DISABLED( 0 ) ) );
  }

  void encode( const wav_frame_t & wav_frame, opus_frame_t & opus_frame )
  {
    if ( wav_frame.size() != NUM_CHANNELS * NUM_SAMPLES_IN_OPUS_FRAME ) {
      throw runtime_error( "wav_frame is not 20 ms long" );
    }

    opus_frame.first = opus_check( opus_encode( encoder_.get(),
                                                wav_frame.data(),
                                                NUM_SAMPLES_IN_OPUS_FRAME,
                                                opus_frame.second.data(),
                                                opus_frame.second.size() ) );
  }
};

/* wrap WAV file with error/validity checks */
class WavWrapper
{
  SndfileHandle handle_;
  vector<int16_t> samples_;

public:
  WavWrapper( const string & filename )
    : handle_( filename ),
      samples_( NUM_CHANNELS * ( NUM_SAMPLES_IN_INPUT + EXPECTED_LOOKAHEAD ) )
  {
    if ( handle_.error() ) {
      throw runtime_error( filename + ": " + handle_.strError() );
    }

    if ( handle_.format() != (SF_FORMAT_WAV | SF_FORMAT_PCM_16) ) {
      throw runtime_error( filename + ": not a 16-bit PCM WAV file" );
    }

    if ( handle_.samplerate() != SAMPLE_RATE ) {
      throw runtime_error( filename + " sample rate is " + to_string( handle_.samplerate() ) + ", not " + to_string( SAMPLE_RATE ) );
    }

    if ( handle_.channels() != NUM_CHANNELS ) {
      throw runtime_error( filename + " channel # is " + to_string( handle_.channels() ) + ", not " + to_string( NUM_CHANNELS ) );
    }

    if ( handle_.frames() != NUM_SAMPLES_IN_INPUT ) {
      throw runtime_error( filename + " length is " + to_string( handle_.frames() ) + ", not " + to_string( NUM_SAMPLES_IN_INPUT ) + " samples" );
    }

    /* read file into memory */
    const auto retval = handle_.read( samples_.data(), NUM_CHANNELS * NUM_SAMPLES_IN_INPUT );
    if ( retval != NUM_CHANNELS * NUM_SAMPLES_IN_INPUT ) {
      throw runtime_error( "unexpected read of " + to_string( retval ) + " samples" );
    }

    /* verify EOF */
    int16_t dummy;
    if ( 0 != handle_.read( &dummy, 1 ) ) {
      throw runtime_error( "unexpected extra data in WAV file" );
    }
  }

  wav_frame_t view( const size_t offset )
  {
    if ( offset > samples_.size() ) {
      throw out_of_range( "offset > samples_.size()" );
    }

    const size_t member_length = NUM_CHANNELS * NUM_SAMPLES_IN_OPUS_FRAME;

    if ( offset + member_length > samples_.size() ) {
      throw out_of_range( "offset + len > samples_.size()" );
    }

    /* second bounds check */
    int16_t first_sample __attribute((unused)) = samples_.at( offset );
    int16_t last_sample __attribute((unused)) = samples_.at( offset + member_length - 1 );

    return { samples_.data() + offset, member_length };
  }
};

class AVFormatWrapper
{
  struct av_deleter { void operator()( AVFormatContext * x ) const { avformat_free_context( x ); } };
  unique_ptr<AVFormatContext, av_deleter> context_ {};

  AVStream * audio_stream_;

  bool header_written_;

  static int av_check( const int retval )
  {
    static array<char, 256> errbuf;

    if ( retval < 0 ) {
      if ( av_strerror( retval, errbuf.data(), errbuf.size() ) < 0 ) {
        throw runtime_error( "av_strerror: error code not found" );
      }

      errbuf.back() = 0;

      throw runtime_error( "libav error: " + string( errbuf.data() ) );
    }

    return retval;
  }

public:
  AVFormatWrapper( const string & output_filename, const int bit_rate )
    : context_(),
      audio_stream_(),
      header_written_( false )
  {
    av_register_all();

    if ( output_filename.substr( output_filename.size() - 5 ) != ".webm" ) {
      throw runtime_error( "output filename must be a .webm" );
    }

    {
      AVFormatContext * tmp_context;
      av_check( avformat_alloc_output_context2( &tmp_context, nullptr, nullptr, output_filename.c_str() ) );
      context_.reset( tmp_context );
    }

    /* open output file */
    av_check( avio_open( &context_->pb, output_filename.c_str(), AVIO_FLAG_WRITE ) );

    /* allocate audio stream */
    audio_stream_ = notnull( "avformat_new_stream",
                             avformat_new_stream( context_.get(), nullptr ) );

    if ( audio_stream_ != context_->streams[ 0 ] ) {
      throw runtime_error( "unexpected stream index != 0" );
    }

    audio_stream_->time_base = { 1, WEBM_TIMEBASE };
    audio_stream_->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
    audio_stream_->codecpar->codec_id = AV_CODEC_ID_OPUS;
    audio_stream_->codecpar->bit_rate = bit_rate;
    audio_stream_->codecpar->bits_per_coded_sample = 16;
    audio_stream_->codecpar->channels = NUM_CHANNELS;
    audio_stream_->codecpar->sample_rate = SAMPLE_RATE;
    audio_stream_->codecpar->initial_padding = 0;
    audio_stream_->codecpar->trailing_padding = 0;

    /* write OpusHead structure as private data -- required by https://wiki.xiph.org/MatroskaOpus,
       the unofficial Opus-in-WebM spec, and enforced by libnestegg (used by Firefox) */

    struct __attribute__ ((packed)) OpusHead
    {
      array<char, 8> signature = { 'O', 'p', 'u', 's', 'H', 'e', 'a', 'd' };
      uint8_t version = 1;
      uint8_t channels = NUM_CHANNELS;
      uint16_t pre_skip = htole16( 0 );
      uint32_t input_sample_rate = htole32( SAMPLE_RATE );
      uint16_t output_gain = htole16( 0 );
      uint8_t channel_mapping_family = 0;
    } opus_head;

    static_assert( sizeof( opus_head ) == 19 );

    audio_stream_->codecpar->extradata = reinterpret_cast<uint8_t *>( notnull( "av_malloc", av_malloc( 19 + AV_INPUT_BUFFER_PADDING_SIZE ) ) );
    audio_stream_->codecpar->extradata_size = 19;
    memcpy( audio_stream_->codecpar->extradata, &opus_head, sizeof( OpusHead ) );

    /* now write the header */
    av_check( avformat_write_header( context_.get(), nullptr ) );
    header_written_ = true;

    if ( audio_stream_->time_base.num != 1
         or audio_stream_->time_base.den != WEBM_TIMEBASE ) {
      throw runtime_error( "audio stream time base mismatch" );
    }
  }

  ~AVFormatWrapper()
  {
    try {
      if ( header_written_ ) {
        av_check( av_write_trailer( context_.get() ) );
      }

      if ( context_->pb ) {
        av_check( avio_close( context_->pb ) );
      }
    } catch ( const exception & e ) {
      cerr << "Exception in AVFormatWrapper destructor: " << e.what() << "\n";
    }
  }

  void write( opus_frame_t & opus_frame,
              const unsigned int starting_sample_number )
  {
    AVPacket packet {};
    packet.buf = nullptr;
    packet.pts = WEBM_TIMEBASE * starting_sample_number / SAMPLE_RATE;
    packet.dts = WEBM_TIMEBASE * starting_sample_number / SAMPLE_RATE;
    packet.data = opus_frame.second.data();
    packet.size = opus_frame.first;
    packet.stream_index = 0;
    packet.flags = AV_PKT_FLAG_KEY;
    packet.duration = WEBM_TIMEBASE * NUM_SAMPLES_IN_OPUS_FRAME / SAMPLE_RATE;
    packet.pos = -1;

    av_check( av_write_frame( context_.get(), &packet ) );
  }

  AVFormatWrapper( const AVFormatWrapper & other ) = delete;
  AVFormatWrapper & operator=( const AVFormatWrapper & other ) = delete;
};

void opus_encode( int argc, char *argv[] ) {
  if ( argc != 5 ) {
    throw runtime_error( "Usage: " + string( argv[ 0 ] ) + " WAV_INPUT WEBM_OUTPUT -b BIT_RATE [e.g., \"64k\"]" );
  }

  /* parse arguments */
  const string input_filename = argv[ 1 ];
  const string output_filename = argv[ 2 ];
  const string dash_b = argv[ 3 ];

  if ( dash_b != "-b" ) {
    throw runtime_error( "-b argument is mandatory" );
  }

  const AudioFormat audio_format { argv[ 4 ] };

  if ( audio_format.bitrate <= 0 or audio_format.bitrate > 256 ) {
    throw runtime_error( "invalid bit rate: " + string( argv[ 4 ] ) );
  }

  const int bit_rate = audio_format.bitrate * 1000; /* bits per second */

  /* open input WAV file */
  WavWrapper wav_file { input_filename };

  /* create Opus encoder */
  OpusEncoderWrapper encoder { bit_rate };

  /* allocate memory for 20 ms of compressed Opus output */
  opus_frame_t opus_frame;

  /* create .webm output */
  AVFormatWrapper output { output_filename, bit_rate };

  /* encode the whole file, outputting every frame except the first,
     and with prediction disabled until frame #2 */

  encoder.disable_prediction();

  for ( unsigned int frame_no = 0; frame_no < NUM_FRAMES_IN_OUTPUT + EXTRA_FRAMES_PREPENDED; frame_no++ ) {
    if ( frame_no == EXTRA_FRAMES_PREPENDED ) {
      encoder.enable_prediction();
    }

    if ( frame_no == NUM_FRAMES_IN_OUTPUT + EXTRA_FRAMES_PREPENDED - 1 ) {
      encoder.disable_prediction();
    }

    encoder.encode( wav_file.view( frame_no * NUM_CHANNELS * NUM_SAMPLES_IN_OPUS_FRAME ), opus_frame );

    if ( frame_no >= EXTRA_FRAMES_PREPENDED ) {
      output.write( opus_frame, (frame_no - EXTRA_FRAMES_PREPENDED) * NUM_SAMPLES_IN_OPUS_FRAME );
    }
  }
}

int main( int argc, char *argv[] )
{
  if ( argc <= 0 ) {
    abort();
  }

  try {
    opus_encode( argc, argv );
  } catch ( const exception & e ) {
    cerr << argv[ 0 ] << ": " << e.what() << "\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
