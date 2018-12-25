/* -*-mode:c++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */

#include <memory>
#include <iostream>
#include <vector>

#include <sndfile.hh>
#include <opus/opus.h>

extern "C" {
#include <libavformat/avformat.h>
}

const unsigned int SAMPLE_RATE = 48000; /* Hz */
const unsigned int NUM_CHANNELS = 2;
const unsigned int NUM_SAMPLES_IN_OPUS_FRAME = 960;
const unsigned int NUM_SAMPLES_IN_FILE = 230400; /* 48kHz * 4.8s */
const unsigned int EXPECTED_LOOKAHEAD = 312; /* 6.5 ms * 48 kHz */
const unsigned int MAX_COMPRESSED_FRAME_SIZE = 131072; /* bytes */
const unsigned int SILENT_SAMPLES_TO_PREPEND = NUM_SAMPLES_IN_OPUS_FRAME - EXPECTED_LOOKAHEAD;

static_assert( SILENT_SAMPLES_TO_PREPEND < NUM_SAMPLES_IN_OPUS_FRAME );

/* make sure file length is integer number of Opus frames */
static_assert( (NUM_SAMPLES_IN_FILE / NUM_SAMPLES_IN_OPUS_FRAME) * NUM_SAMPLES_IN_OPUS_FRAME
               == NUM_SAMPLES_IN_FILE );

const unsigned int NUM_FRAMES_IN_FILE = NUM_SAMPLES_IN_FILE / NUM_SAMPLES_IN_OPUS_FRAME;

/* make sure file is long enough */
static_assert( NUM_SAMPLES_IN_FILE > 4 * NUM_SAMPLES_IN_OPUS_FRAME );

/* view of raw input, and storage for compressed output */
using wav_frame_t = std::basic_string_view<int16_t>;
using opus_frame_t = std::pair<size_t, std::array<uint8_t, MAX_COMPRESSED_FRAME_SIZE>>; // length, buffer

/*

Theory of operation.

Starting with:

   chunk #0: samples 0      .. 230399 (4.8 s)
   chunk #1: samples 230400 .. 460799 (4.8 s)

Then prepend 648 samples of silence to each chunk:

   chunk #0: 648 silent + 0      .. 230399 (4.8135 s)
   chunk #1: 648 silent + 230400 .. 460799 (4.8135 s)

Now encode as Opus with first two frames independent:

 chunk #0:
   frame 0 (independent): 312 of ignore, then 648 silent                     (chop!)
   frame 1 (independent):                0      .. 959
   frame 2              :                960    .. 1919
   frame 3              :                1920   .. 2879
   ...
   frame 240            :                229400 .. 230399

 chunk #1:
   frame 0 (independent): 312 of ignore, then 648 silent                     (chop!)
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

So, for gapless playback, we prepend 648 silent samples, then encode
the first two frames as independent (no prediction), and chop the
first of them.

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
  OpusEncoderWrapper()
  {
    int out;

    /* create encoder */
    encoder_.reset( notnull( "opus_encoder_create",
                             opus_encoder_create( SAMPLE_RATE, NUM_CHANNELS, OPUS_APPLICATION_AUDIO, &out ) ) );
    opus_check( out );

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
      samples_( NUM_CHANNELS * ( SILENT_SAMPLES_TO_PREPEND + NUM_SAMPLES_IN_FILE ) )
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

    if ( handle_.frames() != NUM_SAMPLES_IN_FILE ) {
      throw runtime_error( filename + " length is " + to_string( handle_.frames() ) + ", not " + to_string( NUM_SAMPLES_IN_FILE ) + " samples" );
    }

    /* read file into memory */
    const auto retval = handle_.read( samples_.data() + NUM_CHANNELS * SILENT_SAMPLES_TO_PREPEND, NUM_CHANNELS * NUM_SAMPLES_IN_FILE );
    if ( retval != NUM_CHANNELS * NUM_SAMPLES_IN_FILE ) {
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
  struct av_deleter { void operator()( AVFormatContext * x ) const { av_free( x ); } };
  unique_ptr<AVFormatContext, av_deleter> context_ {};

public:
  AVFormatWrapper()
    : context_( notnull( "avformat_alloc_context", avformat_alloc_context() ) )
  {}
};

void write( const opus_frame_t & opus_frame )
{
  if ( 1 != fwrite( opus_frame.second.data(), opus_frame.first, 1, stdout ) ) {
    throw runtime_error( "error on write" );
  }
}

void opus_encode( int argc, char *argv[] ) {
  if ( argc != 2 ) {
    throw runtime_error( "Usage: " + string( argv[ 0 ] ) + " WAV_FILE" );
  }

  /* open input WAV file */
  WavWrapper wav_file { argv[ 1 ] };

  /* create Opus encoder */
  OpusEncoderWrapper encoder;

  /* allocate memory for 20 ms of compressed Opus output */
  opus_frame_t opus_frame;

  /* create .mkv output */
  AVFormatWrapper output;

  /* encode the whole file, outputting every frame except the first,
     and with prediction disabled until frame #2 */

  encoder.disable_prediction();

  for ( unsigned int frame_no = 0; frame_no < NUM_FRAMES_IN_FILE; frame_no++ ) {
    if ( frame_no == 2 ) {
      encoder.enable_prediction();
    }

    encoder.encode( wav_file.view( frame_no * NUM_CHANNELS * NUM_SAMPLES_IN_OPUS_FRAME ), opus_frame );

    if ( frame_no > 0 ) {
      write( opus_frame );
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
