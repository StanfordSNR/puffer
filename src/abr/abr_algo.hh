#ifndef ABR_ALGO_HH
#define ABR_ALGO_HH

#include "media_formats.hh"
#include "yaml.hh"

class WebSocketClient;

static const double INVALID_SSIM_DB = -4;
static const double MAX_SSIM = 60;
static const double MIN_SSIM = 0;
double ssim_db(const double ssim);

class ABRAlgo
{
public:
  struct Chunk {
    VideoFormat format;   /* chunk format */
    double ssim;          /* chunk ssim */
    unsigned int size;    /* chunk size */
    uint64_t trans_time;  /* transmission time */
    uint32_t cwnd;        /* congestion window (packets) */
    uint32_t in_flight;   /* packets "in flight" */
    uint32_t min_rtt;     /* minimum RTT in microsecond */
    uint32_t rtt;         /* RTT in microsecond */
    uint64_t delivery_rate;  /* bytes per second */
  };

  virtual ~ABRAlgo() {}

  virtual void video_chunk_acked(Chunk &&) {}
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
