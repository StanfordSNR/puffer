#ifndef ABR_ALGO_HH
#define ABR_ALGO_HH

class WebSocketClient;

class ABRAlgo
{
public:
  std::string abr_name() const { return abr_name_; }

protected:
  ABRAlgo(const WebSocketClient & client, const std::string & abr_name)
    : client_(client), abr_name_(abr_name) {}

  /* it is safe to hold a reference to the parent as the parent lives longer */
  const WebSocketClient & client_;
  std::string abr_name_;
};

#endif /* ABR_ALGO_HH */
