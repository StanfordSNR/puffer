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

/* epoll wrapper: instantiate this class on the heap as shared_ptr only */
class Epoller : public std::enable_shared_from_this<Epoller>
{
public:
  /* callback function should return 0 on success and -1 on error */
  using callback_t = std::function<int(void)>;

  Epoller();
  ~Epoller();

  /* file descriptor of the epoll instance */
  int fd_num() const { return epoller_fd_; }

  /* register fd and monitor events on fd */
  void register_fd(const std::shared_ptr<FileDescriptor> & fd_ptr,
                   const uint32_t events);

  /* modify the events mnitored on fd */
  void modify_events(const int fd, const uint32_t events);

  /* set callback function to run when (any of) events occur on fd */
  void set_callback(const int fd,
                    const uint32_t events,
                    const callback_t & callback);

  /* deregister fd from the epoll instance and detach epoller from fd */
  void deregister_fd(const int fd);

  /* return the number of fds that become ready in the epoll list */
  int poll(const int timeout_ms);

  /* forbid copying Epoller objects or assigning them */
  Epoller(const Epoller & other) = delete;
  const Epoller & operator=(const Epoller & other) = delete;

private:
  int epoller_fd_;

  std::unordered_map<int /* fd */, std::weak_ptr<FileDescriptor>> fd_table_;
  std::unordered_map<int /* fd */,
      std::unordered_map<uint32_t /* events */, callback_t>> callback_table_;

  struct epoll_event event_list_[MAX_EPOLL_EVENTS];

  inline void epoll_control(const int op, const int fd, const uint32_t events);

  /* return a shared pointer to the corresponding FileDescriptor object,
   * and nullptr if the object has gone */
  std::shared_ptr<FileDescriptor> get_fd_ptr(const int fd);
};

#endif /* EPOLLER_HH */
