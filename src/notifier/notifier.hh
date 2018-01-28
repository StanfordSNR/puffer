#ifndef NOTIFIER_HH
#define NOTIFIER_HH

#include <cstdint>
#include <vector>
#include <string>
#include <unordered_map>
#include <functional>
#include <tuple>

#include "file_descriptor.hh"
#include "poller.hh"

class Notifier
{
public:
  /* callback function type; args: inotify event and path */
  using callback_t = std::function<void(const inotify_event &,
                                        const std::string &)>;

  Notifier();

  /* add one or more paths to the watch list */
  int add_watch(const std::string & path,
                const uint32_t mask,
                const callback_t & callback);

  std::vector<int> add_watch(const std::vector<std::string> & paths,
                             const uint32_t mask,
                             const callback_t & callback);

  /* remove a watch descriptor from the watch list */
  void rm_watch(const int wd);

  /* poll for timeout; call the registered callback function if an event occurs */
  void poll(const int timeout_ms);

private:
  /* inotify instance */
  FileDescriptor inotify_fd_;

  /* map a watch descriptor to its associated pathname */
  std::unordered_map<int, std::tuple<std::string, uint32_t, callback_t>> imap_;

  Poller poller_;

  Poller::Action::Result handle_events();
};

#endif /* NOTIFIER_HH */
