/* -*-mode:c++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */

#ifndef FILE_DESCRIPTOR_HH
#define FILE_DESCRIPTOR_HH

#include <unistd.h>

#include <string>
#include <unordered_map>
#include <memory>

#include "config.h"

/* maximum size of a read */
static constexpr size_t BUFFER_SIZE = 1024 * 1024;

class Epoller;

/* Unix file descriptors (sockets, files, etc.) */
class FileDescriptor
{
private:
  int fd_;
  bool eof_;

  unsigned int read_count_, write_count_;

  std::unordered_map<int, std::weak_ptr<Epoller>> epollers_;

protected:
  void register_read( void ) { read_count_++; }
  void register_write( void ) { write_count_++; }
  void register_service( const bool write ) { write ? write_count_++ : read_count_++; }
  void set_eof( bool eof = true ) { eof_ = eof; }

public:
  /* construct from fd number */
  FileDescriptor( const int fd );

  /* move constructor */
  FileDescriptor( FileDescriptor && other );

  /* close method throws exception on failure */
  void close();

  /* destructor tries to close, but catches exception */
  virtual ~FileDescriptor();

  /* accessors */
  const int & fd_num( void ) const { return fd_; }
  const bool & eof( void ) const { return eof_; }
  unsigned int read_count( void ) const { return read_count_; }
  unsigned int write_count( void ) const { return write_count_; }

  /* read and write methods */
  std::string read( const size_t limit = BUFFER_SIZE );
  std::string read_exactly( const size_t length, const bool fail_silently = false );
  std::string::const_iterator write( const std::string & buffer, const bool write_all = true );
  std::string::const_iterator write( const std::string::const_iterator & begin,
                                     const std::string::const_iterator & end );

  /* manipulate file offset */
  uint64_t seek(const int64_t offset, const int whence);
  uint64_t curr_offset();
  uint64_t inc_offset(const int64_t offset);

  uint64_t filesize();

  /* reset file offset to the beginning and set EOF to false */
  void reset();

  /* block on an exclusive lock */
  void block_for_exclusive_lock();
  void release_flock();

  /* set nonblocking/blocking behavior */
  void set_blocking( const bool block );

  /* attach and detach Epollers */
  void attach_epoller(const std::shared_ptr<Epoller> & epoller_ptr);
  void detach_epoller(const std::shared_ptr<Epoller> & epoller_ptr);

  /* forbid copying FileDescriptor objects or assigning them */
  FileDescriptor( const FileDescriptor & other ) = delete;
  const FileDescriptor & operator=( const FileDescriptor & other ) = delete;
};

#endif /* FILE_DESCRIPTOR_HH */
