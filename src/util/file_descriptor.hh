/* -*-mode:c++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */

#ifndef FILE_DESCRIPTOR_HH
#define FILE_DESCRIPTOR_HH

#include <string>
#include <unistd.h>

#include "config.h"

#ifdef HAVE_STRING_VIEW
#include <string_view>
#elif HAVE_EXPERIMENTAL_STRING_VIEW
#include <experimental/string_view>
using std::experimental::string_view;
#endif

/* maximum size of a read */
static constexpr size_t BUFFER_SIZE = 1024 * 1024;

/* Unix file descriptors (sockets, files, etc.) */
class FileDescriptor
{
private:
  int fd_;
  bool eof_;

  unsigned int read_count_, write_count_;

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

  /* move assignment */
  FileDescriptor & operator=(FileDescriptor && other);

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
  std::string_view::const_iterator write( const std::string_view & buffer, const bool write_all = true );
  std::string_view::const_iterator write( const std::string_view::const_iterator & begin,
                                          const std::string_view::const_iterator & end );

  /* manipulate file offset */
  uint64_t seek(const int64_t offset, const int whence);
  uint64_t curr_offset();
  uint64_t inc_offset(const int64_t offset);
  void reset_offset();  /* also set EOF to false */

  uint64_t filesize();

  /* flock related */
  void acquire_exclusive_flock();
  void acquire_shared_flock();
  void release_flock();

  /* set nonblocking/blocking behavior */
  void set_blocking( const bool block );

  /* forbid copying FileDescriptor objects or assigning them */
  FileDescriptor( const FileDescriptor & other ) = delete;
  const FileDescriptor & operator=( const FileDescriptor & other ) = delete;
};

#endif /* FILE_DESCRIPTOR_HH */
