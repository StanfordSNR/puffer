/* -*-mode:c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <sys/socket.h>
#include <netinet/in.h>
#include <linux/tcp.h>
#include <linux/netfilter_ipv4.h>
#include <cstring>

#include "socket.hh"
#include "exception.hh"

using namespace std;

/* max name length of congestion control algorithm */
static const size_t TCP_CC_NAME_MAX = 16;

/* default constructor for socket of (subclassed) domain and type */
Socket::Socket( const int domain, const int type )
    : FileDescriptor( CheckSystemCall( "socket", socket( domain, type, 0 ) ) )
{}

/* construct from file descriptor */
Socket::Socket( FileDescriptor && fd, const int domain, const int type )
    : FileDescriptor( move( fd ) )
{
    int actual_value;
    socklen_t len;

    /* verify domain */
    len = getsockopt( SOL_SOCKET, SO_DOMAIN, actual_value );
    if ( (len != sizeof( actual_value )) or (actual_value != domain) ) {
        throw runtime_error( "socket domain mismatch" );
    }

    /* verify type */
    len = getsockopt( SOL_SOCKET, SO_TYPE, actual_value );
    if ( (len != sizeof( actual_value )) or (actual_value != type) ) {
        throw runtime_error( "socket type mismatch" );
    }
}

/* get the local or peer address the socket is connected to */
Address Socket::get_address( const std::string & name_of_function,
                             const std::function<int(int, sockaddr *, socklen_t *)> & function ) const
{
    Address::raw address;
    socklen_t size = sizeof( address );

    CheckSystemCall( name_of_function, function( fd_num(),
                                                 &address.as_sockaddr,
                                                 &size ) );

    return Address( address, size );
}

Address Socket::local_address( void ) const
{
    return get_address( "getsockname", getsockname );
}

Address Socket::peer_address( void ) const
{
    return get_address( "getpeername", getpeername );
}

/* bind socket to a specified local address (usually to listen/accept) */
void Socket::bind( const Address & address )
{
    CheckSystemCall( "bind", ::bind( fd_num(),
                                     &address.to_sockaddr(),
                                     address.size() ) );
}

/* connect socket to a specified peer address */
void Socket::connect( const Address & address )
{
    CheckSystemCall( "connect", ::connect( fd_num(),
                                           &address.to_sockaddr(),
                                           address.size() ) );
    register_write();
}

bool UDPSocket::check_bytes_sent(const ssize_t bytes_sent,
                                 const size_t target) const
{
  if (bytes_sent <= 0) {
    if (bytes_sent == -1 and errno == EWOULDBLOCK) {
      return false; // return false to indicate EWOULDBLOCK
    }

    throw unix_error("UDPSocket:send()/sendto()");
  }

  if (static_cast<size_t>(bytes_sent) != target) {
    throw runtime_error("UDPSocket failed to deliver target number of bytes");
  }

  return true;
}

/* send datagram to connected address */
bool UDPSocket::send(const string_view data)
{
  if (data.empty()) {
    throw runtime_error("attempted to send empty data");
  }

  const ssize_t bytes_sent = ::send(fd_num(), data.data(), data.size(), 0);

  register_write();

  return check_bytes_sent(bytes_sent, data.size());
}

/* send datagram to specified address */
bool UDPSocket::sendto(const Address & dst_addr, const string_view data)
{
  if (data.empty()) {
    throw runtime_error("attempted to send empty data");
  }

  const ssize_t bytes_sent = ::sendto(fd_num(), data.data(), data.size(), 0,
                                      &dst_addr.to_sockaddr(), dst_addr.size());

  register_write();

  return check_bytes_sent(bytes_sent, data.size());
}

/* mark the socket as listening for incoming connections */
void TCPSocket::listen( const int backlog )
{
    CheckSystemCall( "listen", ::listen( fd_num(), backlog ) );
}

/* accept a new incoming connection */
TCPSocket TCPSocket::accept( void )
{
    register_read();
    return TCPSocket( FileDescriptor( CheckSystemCall( "accept", ::accept( fd_num(), nullptr, nullptr ) ) ) );
}

/* get socket option */
template <typename option_type>
socklen_t Socket::getsockopt( const int level, const int option, option_type & option_value ) const
{
    socklen_t optlen = sizeof( option_value );
    CheckSystemCall( "getsockopt", ::getsockopt( fd_num(), level, option,
                                                 &option_value, &optlen ) );
    return optlen;
}

/* set socket option */
template <typename option_type>
void Socket::setsockopt( const int level, const int option, const option_type & option_value )
{
    CheckSystemCall( "setsockopt", ::setsockopt( fd_num(), level, option,
                                                 &option_value, sizeof( option_value ) ) );
}

/* allow local address to be reused sooner, at the cost of some robustness */
void Socket::set_reuseaddr( void )
{
    setsockopt( SOL_SOCKET, SO_REUSEADDR, int( true ) );
}

void Socket::set_reuseport( void )
{
    setsockopt( SOL_SOCKET, SO_REUSEPORT, int( true ) );
}

/* turn on timestamps on receipt */
void UDPSocket::set_timestamps( void )
{
    setsockopt( SOL_SOCKET, SO_TIMESTAMPNS, int( true ) );
}

bool UDPSocket::check_bytes_received(const ssize_t bytes_received) const
{
  if (bytes_received < 0) {
    if (bytes_received == -1 and errno == EWOULDBLOCK) {
      return false; // return false to indicate EWOULDBLOCK
    }

    throw unix_error("UDPSocket:recv()/recvfrom()");
  }

  if (static_cast<size_t>(bytes_received) > UDP_MTU) {
    throw runtime_error("UDPSocket::recv()/recvfrom(): datagram truncated");
  }

  return true;
}

optional<string> UDPSocket::recv()
{
  // data to receive
  vector<char> buf(UDP_MTU);

  const ssize_t bytes_received = ::recv(fd_num(), buf.data(),
                                        UDP_MTU, MSG_TRUNC);

  register_read();

  if (not check_bytes_received(bytes_received)) {
    return nullopt;
  }

  return string{buf.data(), static_cast<size_t>(bytes_received)};
}

pair<Address, optional<string>> UDPSocket::recvfrom()
{
  // data to receive and its source address
  vector<char> buf(UDP_MTU);
  sockaddr src_addr;
  socklen_t src_addr_len = sizeof(src_addr);

  const ssize_t bytes_received = ::recvfrom(
      fd_num(), buf.data(), UDP_MTU, MSG_TRUNC, &src_addr, &src_addr_len);

  register_read();

  if (not check_bytes_received(bytes_received)) {
    return { Address{src_addr, src_addr_len}, nullopt };
  }

  return { Address{src_addr, src_addr_len},
           string{buf.data(), static_cast<size_t>(bytes_received)} };
}

Address TCPSocket::original_dest( void ) const
{
    Address::raw dstaddr;
    socklen_t len = getsockopt( SOL_IP, SO_ORIGINAL_DST, dstaddr );

    return Address( dstaddr, len );
}

void TCPSocket::verify_no_errors() const
{
    int socket_error = 0;
    const socklen_t len = getsockopt( SOL_SOCKET, SO_ERROR, socket_error );
    if ( len != sizeof( socket_error ) ) {
        throw runtime_error( "unexpected length from getsockopt" );
    }

    if ( socket_error ) {
        throw unix_error( "nonblocking socket", socket_error );
    }
}

void TCPSocket::set_congestion_control( const string & cc )
{
    char optval[ TCP_CC_NAME_MAX ];
    strncpy( optval, cc.c_str(), TCP_CC_NAME_MAX );

    try {
      setsockopt( IPPROTO_TCP, TCP_CONGESTION, optval );
    } catch (const exception & e) {
      /* rethrow the exception with better error messages */
      print_exception("set_congestion_control", e);
      throw runtime_error("unavailable congestion control: " + cc);
    }
}

string TCPSocket::get_congestion_control() const
{
    char optval[ TCP_CC_NAME_MAX ];
    getsockopt( IPPROTO_TCP, TCP_CONGESTION, optval );
    return optval;
}

TCPInfo TCPSocket::get_tcp_info() const
{
  /* get tcp_info from the kernel */
  tcp_info x;
  getsockopt( IPPROTO_TCP, TCP_INFO, x );

  /* construct a TCPInfo of our interest */
  TCPInfo ret;
  ret.cwnd = x.tcpi_snd_cwnd;
  ret.in_flight = x.tcpi_unacked - x.tcpi_sacked - x.tcpi_lost + x.tcpi_retrans;
  ret.min_rtt = x.tcpi_min_rtt;
  ret.rtt = x.tcpi_rtt;
  ret.delivery_rate = x.tcpi_delivery_rate;

  return ret;
}
