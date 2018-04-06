#ifndef EPOLLER_HH
#define EPOLLER_HH

#include <sys/epoll.h>
#include <cstdint>

#include <unordered_map>
#include <functional>
#include <memory>

#include "file_descriptor.hh"

/* max events returned by epoll every time */
static constexpr size_t MAX_EPOLL_EVENTS = 128;

/* epoll wrapper: instantiate an epoll instance on the heap only */
class Epoller : public std::enable_shared_from_this<Epoller>
{
public:
  /* callback function should return 0 on success, and -1 on error */
  using callback_t = std::function<int(void)>;

  Epoller();
  ~Epoller();

  /* file descriptor of the epoll instance */
  int fd_num() const { return epoller_fd_; }

  /* register fd and get notified about events on fd */
  void add_events(FileDescriptor & fd, const uint32_t events);

  /* modify the events fd is monitoring */
  void modify_events(FileDescriptor & fd, const uint32_t events);

  /* set callback function to run when (any of) events occur on fd */
  void set_callback(FileDescriptor & fd, const uint32_t events,
                    const callback_t & callback);

  /* deregister fd from the epoll instance and detach epoller from fd */
  void deregister(FileDescriptor & fd);

  /* return the number of fds that become ready in the interest list */
  int poll(const int timeout_ms);

private:
  int epoller_fd_;

  std::unordered_map<int /* fd */,
      std::unordered_map<uint32_t /* events */, callback_t>> callback_table_;

  struct epoll_event event_list_[MAX_EPOLL_EVENTS];

  inline void epoll_control(const int op,
                            FileDescriptor & fd, const uint32_t events);
};

#endif /* EPOLLER_HH */
