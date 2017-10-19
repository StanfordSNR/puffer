#include <stdexcept>
#include <sys/inotify.h>
#include <limits.h>
#include <unistd.h>

#include "exception.hh"
#include "notifier.hh"

using namespace std;
using namespace PollerShortNames;

Notifier::Notifier()
  : inotify_fd_(CheckSystemCall("inotify_init1",
                                inotify_init1(IN_NONBLOCK))),
    imap_(), poller_()
{
  poller_.add_action(
    Poller::Action(inotify_fd_, Direction::In,
      [&]() {
        return handle_events();
      }
    )
  );
}

int Notifier::add_watch(const string & path,
                        const uint32_t mask,
                        const callback_t & callback)
{
  int wd = CheckSystemCall("inotify_add_watch",
               inotify_add_watch(inotify_fd_.fd_num(), path.c_str(), mask));
  imap_[wd] = make_tuple(path, mask, callback);

  return wd;
}

vector<int> Notifier::add_watch(const vector<string> & paths,
                                const uint32_t mask,
                                const callback_t & callback)
{
  vector<int> wd_list;
  for (const auto & path: paths) {
    wd_list.emplace_back(add_watch(path, mask, callback));
  }

  return wd_list;
}

void Notifier::rm_watch(const int wd)
{
  CheckSystemCall("inotify_rm_watch",
                  inotify_rm_watch(inotify_fd_.fd_num(), wd));

  size_t erase_ret = imap_.erase(wd);
  if (erase_ret == 0) {
    throw runtime_error(
      "rm_watch: trying to remove a nonexistent watch descriptor");
  }
}

void Notifier::loop()
{
  while (true) {
    poller_.poll(-1);
  }
}

Result Notifier::handle_events()
{
  const int BUF_LEN = 10 * (sizeof(inotify_event) + NAME_MAX + 1);
  const inotify_event * event;

  while (true) {
    string buffer;
    ssize_t bytes_read = inotify_fd_.read(buffer, BUF_LEN);

    if (bytes_read <= 0) {
      break;
    }

    const char * buf = buffer.c_str();

    for (const char * ptr = buf; ptr < buf + bytes_read; ) {
      event = reinterpret_cast<const inotify_event *>(ptr);

      auto map_it = imap_.find(event->wd);
      if (map_it == imap_.end()) {
        throw runtime_error(
          "inotify event returns a nonexistent watch descriptor");
      }

      string path;
      uint32_t mask;
      callback_t callback;
      tie(path, mask, callback) = map_it->second;

      if ((event->mask & mask) == 0) {
        throw runtime_error("inotify returns non-registered events");
      }

      callback(event, path);

      ptr += sizeof(inotify_event) + event->len;
    }
  }

  return Result();
}
