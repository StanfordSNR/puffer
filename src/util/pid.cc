#include "pid.hh"
#include <exception.hh>

using namespace std;

pid_t pid()
{
  auto pid = CheckSystemCall("getpid", getpid());

  return pid;
}

