#include "ws_client.hh"
#include "channel.hh"

using namespace std;

class ABRAlgo
{
public:
  ABRAlgo() {}
  virtual ~ABRAlgo() {}

  /* check whether the algorithm is ready currently */
  virtual bool is_ready(const WebSocketClient & client,
                        const Channel & channel) = 0;

  virtual const VideoFormat & select_video_quality(const WebSocketClient & client,
                                                   const Channel & channel) = 0;
};
