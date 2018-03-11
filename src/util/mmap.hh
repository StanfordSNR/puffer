#ifndef MMAP_HH
#define MMAP_HH

#include <sys/mman.h>
#include <memory>

std::shared_ptr<void> mmap_shared(void *addr, size_t length, int prot,
                                  int flags, int fd, off_t offset);

#endif /* MMAP_HH */
