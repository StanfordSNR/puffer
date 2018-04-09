#include "epoller.hh"

#include <unistd.h>
#include <iostream>

#include "exception.hh"

using namespace std;

Epoller::Epoller()
  : epoller_fd_(CheckSystemCall("epoll_create1", epoll_create1(EPOLL_CLOEXEC))),
    fd_table_(),
    callback_table_(),
    fds_to_deregister_()
{}

Epoller::~Epoller()
{
  if (close(epoller_fd_) < 0) {
    cerr << "Epoller: failed to close epoll instance " << epoller_fd_ << endl;
  }
}

inline void Epoller::epoll_control(const int op, const int fd,
                                   const uint32_t events)
{
  struct epoll_event ev;
  ev.data.fd = fd;
  ev.events = events;
  CheckSystemCall("epoll_ctl", epoll_ctl(epoller_fd_, op, fd, &ev));
}

shared_ptr<FileDescriptor> Epoller::get_fd_ptr(const int fd)
{
  /* throw an exception if fd does not exist in the epoll list */
  auto it = fd_table_.find(fd);
  if (it == fd_table_.end()) {
    throw runtime_error("Epoller::get_fd_ptr: fd " + to_string(fd) +
                        " does not exist in the epoll list");
  }

  const auto & fd_weak_ptr = it->second;
  return fd_weak_ptr.lock();
}

void Epoller::register_fd(const std::shared_ptr<FileDescriptor> & fd_ptr,
                          const uint32_t events)
{
  if (events == 0) {
    throw runtime_error("Epoller::register_fd: empty events");
  }

  /* add fd to the epoll list */
  int fd = fd_ptr->fd_num();
  epoll_control(EPOLL_CTL_ADD, fd, events);

  /* add weak_ptr<FileDescriptor> to fd_table_ */
  auto ret = fd_table_.emplace(fd, fd_ptr);
  if (not ret.second) {
    throw runtime_error("Epoller::register_fd: fd " + to_string(fd) +
                        " already exists in the epoll list");
  }

  /* attach the epoll instance to fd */
  fd_ptr->attach_epoller(shared_from_this());
}

void Epoller::modify_events(const int fd, const uint32_t events)
{
  if (events == 0) {
    throw runtime_error("Epoller::modify_events: empty events");
  }

  if (get_fd_ptr(fd) == nullptr) {
    throw runtime_error(
      "Epoller::modify_events: the FileDescriptor object of fd " +
      to_string(fd) + " has gone");
  }

  /* modify events monitored on fd */
  epoll_control(EPOLL_CTL_MOD, fd, events);
}

void Epoller::set_callback(const int fd,
                           const uint32_t events,
                           const callback_t & callback)
{
  if (events == 0) {
    throw runtime_error("Epoller::set_callback: empty events");
  }

  if (get_fd_ptr(fd) == nullptr) {
    throw runtime_error(
      "Epoller::set_callback: the FileDescriptor object of fd " +
      to_string(fd) + " has gone");
  }

  auto it = callback_table_.find(fd);

  /* check if fd and events both already exist */
  if (it != callback_table_.end()) {
    const auto & events_map = it->second;
    if (events_map.find(events) != events_map.end()) {
      throw runtime_error("Epoller::set_callback: adding a callback for "
                          "existing events on fd " + to_string(fd));
    }
  }

  callback_table_[fd][events] = callback;
}

/* tolerant non-fatal errors so fd can be deregistered multiple times */
void Epoller::do_deregister_fd(const int fd)
{
  auto it = fd_table_.find(fd);
  if (it == fd_table_.end()) {
    cerr << "Warning: Epoller: fd " << fd
         << " does not exist in the epoll list" << endl;
    return;
  }

  const auto & fd_weak_ptr = it->second;
  auto fd_ptr = fd_weak_ptr.lock();
  if (fd_ptr) {
    /* detach the epoll instance from fd */
    fd_ptr->detach_epoller(epoller_fd_);
  }

  fd_table_.erase(fd);

  callback_table_.erase(fd);

  /* deregister fd from epoll instance */
  if (epoll_ctl(epoller_fd_, EPOLL_CTL_DEL, fd, nullptr) < 0) {
    cerr << "Warning: Epoller: failed to deregister fd " << fd << endl;
    return;
  }
}

void Epoller::deregister_fd(const int fd)
{
  fds_to_deregister_.emplace(fd);
}

int Epoller::poll(const int timeout_ms)
{
  int nfds = CheckSystemCall("epoll_wait",
      epoll_wait(epoller_fd_, event_list_, sizeof(event_list_), timeout_ms));

  for (int i = 0; i < nfds; i++) {
    int fd = event_list_[i].data.fd;
    uint32_t revents = event_list_[i].events;

    auto fd_ptr = get_fd_ptr(fd);
    if (fd_ptr == nullptr) {
      cerr << "Epoller::poll: the FileDescriptor object of fd " << fd
           << " has gone" << endl;
      deregister_fd(fd);
      continue;
    }

    auto it = callback_table_.find(fd);
    if (it == callback_table_.end()) {
      throw runtime_error("Epoller::poll: fd " + to_string(fd) + " does not "
                          "have any callbacks");
    }

    for (const auto & [events, callback] : it->second) {
      if (revents & events) {
        /* callback returns error */
        if (callback() < 0) {
          cerr << "Epoller::poll: callback error on fd " << fd << endl;
          deregister_fd(fd);
          break;
        }
      }
    }
  }

  /* do the real deregistering */
  for (const int fd_to_deregister : fds_to_deregister_) {
    do_deregister_fd(fd_to_deregister);
  }
  fds_to_deregister_.clear();

  return nfds;
}
