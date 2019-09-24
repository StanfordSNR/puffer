/* -*-mode:c++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */

#include "ipc_socket.hh"

#include <sys/socket.h>
#include <sys/un.h>

#include "exception.hh"

using namespace std;

IPCSocket::IPCSocket()
  : FileDescriptor( CheckSystemCall( "socket", socket( AF_UNIX, SOCK_STREAM, 0 ) ) )
{}

sockaddr_un create_sockaddr_un( const string & path )
{
  if ( path.size() >= sizeof( sockaddr_un::sun_path ) ) {
    throw runtime_error( "path size is too long" );
  }

  struct sockaddr_un addr;
  addr.sun_family = AF_UNIX;
  strcpy( addr.sun_path, path.c_str() );
  return addr;
}

void IPCSocket::bind( const string & path )
{
  const sockaddr_un addr = create_sockaddr_un( path );
  CheckSystemCall( "bind", ::bind( fd_num(), ( struct sockaddr *)&addr,
                                   sizeof( sockaddr_un ) ) );
}

void IPCSocket::connect( const string & path )
{
  const sockaddr_un addr = create_sockaddr_un( path );
  CheckSystemCall( "connect", ::connect( fd_num(), ( struct sockaddr *)&addr,
                                         sizeof( sockaddr_un ) ) );
}

void IPCSocket::listen( const int backlog )
{
  CheckSystemCall( "listen", ::listen( fd_num(), backlog ) );
}

FileDescriptor IPCSocket::accept()
{
  return { CheckSystemCall( "accept", ::accept( fd_num(), nullptr, nullptr ) ) };
}

/* set socket option */
template <typename option_type>
void IPCSocket::setsockopt( const int level, const int option, const option_type & option_value )
{
  CheckSystemCall( "setsockopt", ::setsockopt( fd_num(), level, option,
                                               &option_value, sizeof( option_value ) ) );
}

/* allow local address to be reused sooner, at the cost of some robustness */
void IPCSocket::set_reuseaddr( void )
{
  setsockopt( SOL_SOCKET, SO_REUSEADDR, int( true ) );
}
