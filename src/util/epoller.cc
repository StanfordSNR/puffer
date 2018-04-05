#include "epoller.hh"

#include <unistd.h>

#include <iostream>
#include "exception.hh"

using namespace std;

Epoller::Epoller()
  : epoller_fd_(CheckSystemCall("epoll_create1", epoll_create1(EPOLL_CLOEXEC))),
    callback_table_()
{}

Epoller::~Epoller()
{
  if (close(epoller_fd_) < 0) {
    cerr << "Epoller: failed to close epoll instance " << epoller_fd_ << endl;
  }
}

void Epoller::epoll_control(const int op, const int fd, const uint32_t events)
{
  struct epoll_event ev;
  ev.data.fd = fd;
  ev.events = events;
  CheckSystemCall("epoll_ctl", epoll_ctl(epoller_fd_, op, fd, &ev));
}

void Epoller::add_events(const int fd, const uint32_t events)
{
  epoll_control(EPOLL_CTL_ADD, fd, events);
}

void Epoller::delete_events(const int fd, const uint32_t events)
{
  epoll_control(EPOLL_CTL_DEL, fd, events);
}

void Epoller::modify_events(const int fd, const uint32_t events)
{
  epoll_control(EPOLL_CTL_MOD, fd, events);
}

void Epoller::set_callback(const int fd, const uint32_t event,
                           const callback_t & callback)
{
  callback_table_[fd][event] = callback;
}

void Epoller::poll(const int timeout_ms)
{
  int nfds = CheckSystemCall("epoll_wait",
      epoll_wait(epoller_fd_, event_list_, sizeof(event_list_), timeout_ms));

  for (int i = 0; i < nfds; i++) {
    int fd = event_list_[i].data.fd;
    uint32_t revents = event_list_[i].events;

    auto it = callback_table_.find(fd);

    if (it == callback_table_.end()) {
      throw runtime_error("Epoller::poll: unregistered fd " + to_string(fd));
    }

    for (const auto & [event, callback] : it->second) {
      if (revents & event) {
        callback();
      }
    }
  }
}
