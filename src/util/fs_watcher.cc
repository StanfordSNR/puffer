#include <unistd.h>
#include <semaphore.h>
#include <fcntl.h>
#include "fs_watcher.hh"

#define FS_WATCHER_BUFFER_SIZE 4096

using namespace std;

FsEvent::FsEvent(FsEventType type, std::string path):
  type(type), path(path)
{}

FsEvent::FsEvent(): FsEvent(FsEventType::Other, "")
{}

FsEvent::~FsEvent()
{}

FsQueue::FsQueue(): sem_(), queue_()
{
  if(sem_init(&sem_, 0, 0) < 0)
    throw "OS does not support unnamed semaphore";
}

FsEvent FsQueue::pop()
{
  sem_wait(&this->sem_);
  FsEvent event = this->queue_.front();
  this->queue_.pop();
  return event;
}

void FsQueue::push(FsEvent const & e)
{
  this->queue_.push(e);
  sem_post(&this->sem_);
}

FsQueue::~FsQueue()
{
  sem_destroy(&this->sem_);
}

FsWatcher::FsWatcher(const char* const path): queue(), path_(path),
  inotify_fd_(inotify_init()), stop_(true), inotify_t_(nullptr)
{
  if(this->inotify_fd_ < 0)
    throw "Failed to initialize inotify";
  /* listen to fs changes */
  int wd = inotify_add_watch(this->inotify_fd_, path, IN_ALL_EVENTS);
  if(wd < 0)
    throw "Failed to add inotify watch";
}

void FsWatcher::start()
{
  /* set non blocking read */
  int flags = fcntl(this->inotify_fd_, F_GETFL, 0);
  fcntl(this->inotify_fd_, F_SETFL, flags | O_NONBLOCK);

  this->stop_ = false;
  this->inotify_t_ = make_unique<std::thread>(&FsWatcher::run, this);
}

void FsWatcher::terminate()
{
  this->stop_ = true;
  if(this->inotify_t_) {
    this->inotify_t_->join();
    /* let the unique ptr go away */
    this->inotify_t_ = nullptr;
  }
}

FsWatcher::~FsWatcher()
{
  this->terminate();
  if(this->inotify_fd_ > 0)
    close(inotify_fd_);
}

void FsWatcher::run()
{
  char buf[FS_WATCHER_BUFFER_SIZE];
  while(!this->stop_) {
    int len = read(this->inotify_fd_, buf, sizeof buf);
    if(len < 0) {
      if(len == -1 && errno == EAGAIN)
        continue;
      else
        throw "Unable to read FS events";
    }
    /* some pointer arithmetic */
    for(char * p = buf; p < buf + len; ) {
      struct inotify_event * event = (struct inotify_event *) p;
      /* handle the event */
      std::string name = std::string(event->name);
      FsEventType event_type = FsEventType::Other;
      if(event->mask & IN_CREATE)
        event_type = FsEventType::Create;
      else if(event->mask & IN_ACCESS)
        event_type = FsEventType::Access;
      else if(event->mask & IN_DELETE)
        event_type = FsEventType::Delete;
      /* add the event to the event queue
       * The queue will do a copy */
      FsEvent fe(event_type, name);
      this->queue.push(fe);
      p += sizeof(struct inotify_event) + event->len;
    }

  }
}
