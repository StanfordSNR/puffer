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

/* send datagram to specified address */
void UDPSocket::sendto( const Address & destination, const string & payload )
{
    const ssize_t bytes_sent =
        CheckSystemCall( "sendto", ::sendto( fd_num(),
                                             payload.data(),
                                             payload.size(),
                                             0,
                                             &destination.to_sockaddr(),
                                             destination.size() ) );

    register_write();

    if ( size_t( bytes_sent ) != payload.size() ) {
        throw runtime_error( "datagram payload too big for sendto()" );
    }
}

/* send datagram to connected address */
void UDPSocket::send( const string & payload )
{
    const ssize_t bytes_sent =
        CheckSystemCall( "send", ::send( fd_num(),
                                         payload.data(),
                                         payload.size(),
                                         0 ) );

    register_write();

    if ( size_t( bytes_sent ) != payload.size() ) {
        throw runtime_error( "datagram payload too big for send()" );
    }
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

pair<Address, string> UDPSocket::recvfrom( void )
{
    static const ssize_t RECEIVE_MTU = 65536;

    /* receive source address and payload */
    Address::raw datagram_source_address;
    char buffer[ RECEIVE_MTU ];

    socklen_t fromlen = sizeof( datagram_source_address );

    ssize_t recv_len = CheckSystemCall( "recvfrom",
                                        ::recvfrom( fd_num(),
                                                    buffer,
                                                    sizeof( buffer ),
                                                    MSG_TRUNC,
                                                    &datagram_source_address.as_sockaddr,
                                                    &fromlen ) );

    if ( recv_len > RECEIVE_MTU ) {
        throw runtime_error( "recvfrom (oversized datagram)" );
    }

    register_read();

    return make_pair( Address( datagram_source_address, fromlen ),
                      string( buffer, recv_len ) );
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
