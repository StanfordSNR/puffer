#include <exception>
#include <memory>
#include <cstdlib>
#include <iostream>
#include <vector>
#include <array>
#include <sndfile.hh>

#include <opus/opus.h>

#include "file_descriptor.hh"

const unsigned int SAMPLE_RATE = 48000; /* Hz */
const unsigned int NUM_CHANNELS = 2;
const unsigned int NUM_SAMPLES_IN_OPUS_FRAME = 960;
const unsigned int NUM_SAMPLES_IN_FILE = 230400 + NUM_SAMPLES_IN_OPUS_FRAME; /* 48kHz * 4.8s + 20ms */
const unsigned int EXPECTED_LOOKAHEAD = 312; /* 6.5 ms * 48 kHz */
const unsigned int MAX_COMPRESSED_FRAME_SIZE = 131072; /* bytes */

/* make sure file length is integer number of Opus frames */
static_assert( (NUM_SAMPLES_IN_FILE / NUM_SAMPLES_IN_OPUS_FRAME) * NUM_SAMPLES_IN_OPUS_FRAME
	       == NUM_SAMPLES_IN_FILE );

const unsigned int NUM_FRAMES_IN_FILE = NUM_SAMPLES_IN_FILE / NUM_SAMPLES_IN_OPUS_FRAME;

/* make sure file is long enough */
static_assert( NUM_SAMPLES_IN_FILE > 4 * NUM_SAMPLES_IN_OPUS_FRAME );

/* storage for raw input and compressed output */
using wav_frame_t = std::array<int16_t, NUM_SAMPLES_IN_OPUS_FRAME>;
using opus_frame_t = std::pair<size_t, std::array<uint8_t, MAX_COMPRESSED_FRAME_SIZE>>;

/*

Theory of operation. Starting with:

chunk #0: samples 0 .. 4799 (80+20 ms)

frame 0 (independent): 312 of ignore, then 0    .. 647                       (chop!)
frame 1 (independent):                     648  .. 1607
frame 2              :                     1608 .. 2567
frame 3              :                     2568 .. 3527
frame 4              :                     3528 .. 4487
frame 5              :                     4488 .. 4799, then 648 of ignore  (chop!)

chunk #1: samples 3840 .. 8639 (80+20 ms)

frame 0 (independent): 312 of ignore, then 3840 .. 4487                      (chop!)
frame 1 (independent):                     4488 .. 5447
frame 2              :                     5448 .. 6407
frame 3              :                     6408 .. 7367
frame 4              :                     7368 .. 8327
frame 5              :                     8328 .. 8639, then 648 of ignore  (chop!)

Chopping produces:

chunk #0: samples 0 .. 4799 (80+20 ms)

frame 1 (independent):                     648  .. 1607
frame 2              :                     1608 .. 2567
frame 3              :                     2568 .. 3527
frame 4              :                     3528 .. 4487

chunk #1: samples 3840 .. 8639 (80+20 ms)

frame 1 (independent):                     4488 .. 5447
frame 2              :                     5448 .. 6407
frame 3              :                     6408 .. 7367
frame 4              :                     7368 .. 8327

So, for gapless playback, we encode the first two frames as
independent (no prediction), and chop the first of them.  We also chop
the last frame.

*/

using namespace std;

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
    encoder_.reset( opus_encoder_create( SAMPLE_RATE, NUM_CHANNELS, OPUS_APPLICATION_AUDIO, &out ) );
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

public:
  WavWrapper( const string & filename )
    : handle_( filename )
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
  }

  void read_into( wav_frame_t & frame )
  {
    const auto retval = handle_.read( frame.data(), NUM_CHANNELS * NUM_SAMPLES_IN_OPUS_FRAME );
    if ( retval != NUM_CHANNELS * NUM_SAMPLES_IN_OPUS_FRAME ) {
      throw runtime_error( "unexpected read of " + to_string( retval ) + " samples" );
    }
  }

  void verify_eof()
  {
    wav_frame_t dummy;
    if ( 0 != handle_.read( dummy.data(), NUM_CHANNELS * NUM_SAMPLES_IN_OPUS_FRAME ) ) {
      throw runtime_error( "unexpected extra data in WAV file" );
    }
  }
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

  /* allocate memory for 20 ms of WAV input */
  wav_frame_t wav_frame;

  /* allocate memory for 20 ms of compressed Opus output */
  opus_frame_t opus_frame;

  /* step 1: encode two frames with prediction disabled, ignoring the first */
  encoder.disable_prediction();

  /* frame 0 (ignore) */
  wav_file.read_into( wav_frame );
  encoder.encode( wav_frame, opus_frame );

  /* frame 1 */
  wav_file.read_into( wav_frame );
  encoder.encode( wav_frame, opus_frame );
  write( opus_frame );

  /* step 2: encode rest of file with prediction enabled, except the last */
  encoder.enable_prediction();

  for ( unsigned int frame_no = 2; frame_no < NUM_FRAMES_IN_FILE - 1; frame_no++ ) {
    wav_file.read_into( wav_frame );
    encoder.encode( wav_frame, opus_frame );
    write( opus_frame );
  }

  /* step 3: read the final frame, then make sure we're at the end of the file */
  wav_file.read_into( wav_frame );
  wav_file.verify_eof();
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
