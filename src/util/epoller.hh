#ifndef EPOLLER_HH
#define EPOLLER_HH

#include <sys/epoll.h>
#include <cstdint>

#include <unordered_map>
#include <functional>
#include <memory>

#include "file_descriptor.hh"

/* max events returned by epoll every time */
static constexpr size_t MAX_EPOLL_EVENTS = 16 * 1024;

class Epoller : public std::enable_shared_from_this<Epoller>
{
public:
  Epoller();
  ~Epoller();

  int fd_num() const { return epoller_fd_; }

  void add_events(FileDescriptor & fd, const uint32_t events);
  void modify_events(FileDescriptor & fd, const uint32_t events);

  using callback_t = std::function<void(void)>;
  void set_callback(FileDescriptor & fd, const uint32_t event,
                    const callback_t & callback);

  void deregister(FileDescriptor & fd);

  void poll(const int timeout_ms);

private:
  int epoller_fd_;

  std::unordered_map<int, /* fd */
      std::unordered_map<uint32_t /* event */, callback_t>> callback_table_;

  struct epoll_event event_list_[MAX_EPOLL_EVENTS];

  inline void epoll_control(const int op,
                            FileDescriptor & fd, const uint32_t events);
};

#endif /* EPOLLER_HH */
