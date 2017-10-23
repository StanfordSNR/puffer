#include <stdexcept>
#include <sys/inotify.h>
#include <limits.h>
#include <unistd.h>

#include "exception.hh"
#include "notifier.hh"

using namespace std;
using namespace PollerShortNames;

Notifier::Notifier()
  : inotify_fd_(CheckSystemCall("inotify_init", inotify_init())),
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

  /* insert a new key-value pair or update the current value */
  imap_[wd] = make_tuple(path, mask, callback);

  return wd;
}

vector<int> Notifier::add_watch(const vector<string> & paths,
                                const uint32_t mask,
                                const callback_t & callback)
{
  vector<int> wd_list;
  for (const auto & path : paths) {
    wd_list.emplace_back(add_watch(path, mask, callback));
  }

  return wd_list;
}

void Notifier::rm_watch(const int wd)
{
  CheckSystemCall("inotify_rm_watch",
                  inotify_rm_watch(inotify_fd_.fd_num(), wd));

  auto erase_ret = imap_.erase(wd);
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
  /* explicitly ensure the buffer is sufficient to read at least one event */
  const int BUF_LEN = sizeof(inotify_event) + NAME_MAX + 1;

  /* read events */
  string event_buf = inotify_fd_.read(BUF_LEN);

  const char * buf = event_buf.c_str();
  const inotify_event * event;

  /* loop over all events in the buffer */
  for (const char * ptr = buf; ptr < buf + event_buf.size(); ) {
    event = reinterpret_cast<const inotify_event *>(ptr);

    auto imap_it = imap_.find(event->wd);
    if (imap_it == imap_.end()) {
      throw runtime_error(
        "inotify event returns a nonexistent watch descriptor");
    }

    const auto & value_ref = imap_it->second;
    /* use 'get' instead of 'tie' to avoid copy */
    const string & path = get<0>(value_ref);
    const uint32_t mask = get<1>(value_ref);
    const callback_t & callback = get<2>(value_ref);

    if ((event->mask & mask) == 0) {
      throw runtime_error("inotify returns non-registered events");
    }

    /* run the callback function */
    callback(*event, path);

    ptr += sizeof(inotify_event) + event->len;
  }

  return Result();
}
