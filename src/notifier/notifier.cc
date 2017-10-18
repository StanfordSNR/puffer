#include <stdexcept>
#include <sys/inotify.h>

#include "exception.hh"
#include "notifier.hh"

using namespace std;

Notifier::Notifier()
  : inotify_fd_(FileDescriptor(CheckSystemCall("inotify_init1",
                                               inotify_init1(IN_NONBLOCK)))),
    pathname_()
{}

int Notifier::add_watch(const string & path, const uint32_t mask)
{
  int wd = CheckSystemCall("inotify_add_watch",
                           inotify_add_watch(inotify_fd_.fd_num(),
                                             path.c_str(), mask));
  pathname_[wd] = path;

  return wd;
}

vector<int> Notifier::add_watch(const vector<string> & paths,
                                const uint32_t mask)
{
  vector<int> wd_list;
  for (const auto & path: paths) {
    wd_list.emplace_back(add_watch(path, mask));
  }

  return wd_list;
}

void Notifier::rm_watch(const int wd)
{
  CheckSystemCall("inotify_rm_watch",
                  inotify_rm_watch(inotify_fd_.fd_num(), wd));

  size_t erase_ret = pathname_.erase(wd);
  if (erase_ret == 0) {
    throw runtime_error(
      "rm_watch: trying to remove a nonexistent watch descriptor");
  }
}

void Notifier::print_watch_list()
{
  for (const auto & kv : pathname_) {
    cout << kv.second << endl;
  }
}
