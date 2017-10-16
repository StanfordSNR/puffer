#ifndef FS_WATCHER_HH
#define FS_WATCHER_HH

#include <string>
#include <thread>
#include <queue>
#include <atomic>
#include <sys/inotify.h>
#include <string>
#include <memory>
#include <semaphore.h>

enum FsEventType {
  Access,
  Create,
  Delete,
  Other
};

class FsEvent {
public:
  FsEventType type;
  std::string path;
  FsEvent();
  FsEvent(FsEventType type, std::string path);
  ~FsEvent();
};


/* counting semaphore */
class FsQueue {
public:
  void push(FsEvent const & e);
  FsEvent pop();

  FsQueue();
  ~FsQueue();

private:
  sem_t sem_;
  std::queue<FsEvent> queue_;
};


class FsWatcher{
public:
  FsQueue queue;
  FsWatcher(const char * const path);
  void start();
  void terminate();
  ~FsWatcher();
private:
  std::string path_;
  int inotify_fd_;
  std::atomic_bool stop_;
  std::unique_ptr<std::thread> inotify_t_;

  void run();
};

#endif /* __FS_WATCHER_HH__ */
