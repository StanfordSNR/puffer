#ifndef NOTIFIER_HH
#define NOTIFIER_HH

#include <vector>
#include <string>
#include <cstdint>
#include <unordered_map>

#include "file_descriptor.hh"

/* Non-blocking filesystem monitor */
class Notifier
{
public:
  Notifier();

  /* add one or more paths to the watch list */
  int add_watch(const std::string & path, const uint32_t mask);
  std::vector<int> add_watch(const std::vector<std::string> & paths,
                             const uint32_t mask);

  /* remove a watch descriptor from the watch list */
  void rm_watch(const int wd);

  /* print pathnames in the current watch list */
  void print_watch_list();

private:
  /* inotify instance */
  FileDescriptor inotify_fd_;

  /* map a watch descriptor to its associated pathname */
  std::unordered_map<int, std::string> pathname_;
};

#endif /* NOTIFIER_HH */
