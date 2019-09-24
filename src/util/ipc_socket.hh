/* -*-mode:c++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */

#ifndef IPC_SOCKET_HH
#define IPC_SOCKET_HH

#include <string>

#include "file_descriptor.hh"

/* Unix domain socket */
class IPCSocket : public FileDescriptor
{
public:
  IPCSocket();

  void bind( const std::string & path );
  void connect( const std::string & path );

  void listen( const int backlog = 200 );
  FileDescriptor accept( void );

  void set_reuseaddr( void );

protected:
  template <typename option_type>
  void setsockopt( const int level, const int option, const option_type & option_value );
};

#endif /* IPC_SOCKET_HH */
