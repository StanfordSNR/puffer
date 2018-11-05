#ifndef ABR_ALGO_HH
#define ABR_ALGO_HH

#include "media_formats.hh"

class WebSocketClient;

class ABRAlgo
{
public:
  struct Chunk {
    VideoFormat format;
    double ssim;
    unsigned int chunk_size;
    uint64_t trans_time;
  };

  virtual ~ABRAlgo() {}

  virtual void reset() {}
  virtual void video_chunk_acked(const VideoFormat & /* format */,
                                 const double /* ssim */,
                                 const unsigned int /* chunk_size */,
                                 const uint64_t /* transmission_time */) {}
  virtual VideoFormat select_video_format() = 0;

  /* accessors */
  std::string abr_name() const { return abr_name_; }

protected:
  ABRAlgo(const WebSocketClient & client, const std::string & abr_name)
    : client_(client), abr_name_(abr_name) {}

  /* it is safe to hold a reference to the parent as the parent lives longer */
  const WebSocketClient & client_;
  std::string abr_name_;
};

#endif /* ABR_ALGO_HH */
