#include <iostream>
#include <vector>
#include <string>
#include <sys/inotify.h>

#include "notifier.hh"

using namespace std;

void callback(const inotify_event * event, const string & path)
{
  uint32_t mask = event->mask;

  if (mask & IN_ISDIR) {
    cerr << "Directory";
  } else {
    cerr << "File";
  }

  if (mask & IN_CREATE) {
    cerr << " is created" << endl;
  }

  if (mask & IN_CLOSE_WRITE) {
    cerr << " opened for writing is closed" << endl;
  }

  cerr << "Path is " << path << endl;
  if (event->len) {
    cerr << "Filename is " << string(event->name) << endl;
  }
}

void print_usage(const string & program_name)
{
  cerr << "Usage: " << program_name << " PATH [PATH ...]" << endl;
}

int main(int argc, char * argv[])
{
  if (argc < 1) {
    abort();
  }

  if (argc == 1) {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  vector<string> paths;
  for (int i = 1; i < argc; i++) {
    paths.emplace_back(argv[i]);
  }

  Notifier notifier;
  notifier.add_watch(paths, IN_CLOSE_WRITE, callback);
  notifier.loop();

  return EXIT_SUCCESS;
}
