#ifndef ABR_ALGO_HH
#define ABR_ALGO_HH

#include "media_formats.hh"

class WebSocketClient;

class ABRAlgo
{
public:
  virtual ~ABRAlgo() {}

  virtual void reset() = 0;
  virtual VideoFormat select_video_format() = 0;

  /* accessors */
  std::string abr_name() const { return abr_name_; }

  /* const values */
  static const unsigned int MAX_BUFFER_S = 10;  /* seconds */

protected:
  ABRAlgo(const WebSocketClient & client, const std::string & abr_name)
    : client_(client), abr_name_(abr_name) {}

  /* it is safe to hold a reference to the parent as the parent lives longer */
  const WebSocketClient & client_;
  std::string abr_name_;
};

#endif /* ABR_ALGO_HH */
