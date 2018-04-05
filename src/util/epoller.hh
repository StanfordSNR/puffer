#ifndef EPOLLER_HH
#define EPOLLER_HH

#include <sys/epoll.h>
#include <cstdint>

#include <unordered_map>
#include <functional>

/* max events returned by epoll every time */
static constexpr size_t MAX_EPOLL_EVENTS = 16 * 1024;

class Epoller
{
public:
  Epoller();
  ~Epoller();

  void add_events(const int fd, const uint32_t events);
  void delete_events(const int fd, const uint32_t events);
  void modify_events(const int fd, const uint32_t events);

  using callback_t = std::function<void(void)>;
  void set_callback(const int fd, const uint32_t event,
                    const callback_t & callback);

  void poll(const int timeout_ms);

private:
  int epoller_fd_;
  std::unordered_map<int,
                     std::unordered_map<uint32_t, callback_t>> callback_table_;

  struct epoll_event event_list_[MAX_EPOLL_EVENTS];

  void epoll_control(const int op, const int fd, const uint32_t events);
};

#endif /* EPOLLER_HH */
