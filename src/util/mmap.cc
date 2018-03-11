#include "mmap.hh"
#include <exception.hh>

using namespace std;

shared_ptr<void> mmap_shared(void *addr, size_t length, int prot,
                             int flags, int fd, off_t offset)
{
  void * p = mmap(addr, length, prot, flags, fd, offset);
  if (p == MAP_FAILED) {
    throw runtime_error("mmap failed");
  }

  auto deleter = [length](void * mmaped_addr) {
    CheckSystemCall("munmap", munmap(mmaped_addr, length));
  };

  return { p, deleter };
}

