#include <exception>
#include <memory>
#include <cstdlib>
#include <iostream>

#include <opus/opus.h>

using namespace std;
const unsigned int SAMPLE_RATE = 48000;
const unsigned int NUM_CHANNELS = 2;

class OpusEncoderWrapper
{
  struct opus_deleter { void operator()( OpusEncoder * x ) const { opus_encoder_destroy( x ); } };
  unique_ptr<OpusEncoder, opus_deleter> encoder_ {};

public:
  OpusEncoderWrapper()
  {
    int error;
    encoder_.reset( opus_encoder_create( SAMPLE_RATE, NUM_CHANNELS, OPUS_APPLICATION_AUDIO, &error ) );
    if ( error != OPUS_OK ) {
      throw runtime_error( "Opus error: " + string( opus_strerror( error ) ) );
    }
  }
};

int main()
{
  try {
    OpusEncoderWrapper enc;
  } catch ( const exception & e ) {
    cerr << e.what() << "\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
