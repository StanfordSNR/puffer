/* -*-mode:c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef SOCKET_HH
#define SOCKET_HH

#include <functional>

#include "address.hh"
#include "file_descriptor.hh"

/* class for network sockets (UDP, TCP, etc.) */
class Socket : public FileDescriptor
{
private:
    /* get the local or peer address the socket is connected to */
    Address get_address( const std::string & name_of_function,
                         const std::function<int(int, sockaddr *, socklen_t *)> & function ) const;

protected:
    /* default constructor */
    Socket( const int domain, const int type );

    /* construct from file descriptor */
    Socket( FileDescriptor && s_fd, const int domain, const int type );

    /* get and set socket option */
    template <typename option_type>
    socklen_t getsockopt( const int level, const int option, option_type & option_value ) const;

    template <typename option_type>
    void setsockopt( const int level, const int option, const option_type & option_value );

public:
    /* bind socket to a specified local address (usually to listen/accept) */
    void bind( const Address & address );

    /* connect socket to a specified peer address */
    void connect( const Address & address );

    /* accessors */
    Address local_address( void ) const;
    Address peer_address( void ) const;

    /* allow local address to be reused sooner, at the cost of some robustness */
    void set_reuseaddr( void );
    void set_reuseport( void );
};

/* UDP socket */
class UDPSocket : public Socket
{
public:
    UDPSocket() : Socket( AF_INET, SOCK_DGRAM ) {}

    /* receive datagram and where it came from */
    std::pair<Address, std::string> recvfrom( void );

    /* send datagram to specified address */
    void sendto( const Address & peer, const std::string & payload );

    /* send datagram to connected address */
    void send( const std::string & payload );

    /* turn on timestamps on receipt */
    void set_timestamps( void );
};

/* tcp_info of our interest; keep the units used in the kernel */
struct TCPInfo
{
  uint32_t cwnd;      /* congestion window (packets) */
  uint32_t in_flight; /* packets "in flight" */
  uint32_t min_rtt;   /* minimum RTT in microsecond */
  uint32_t rtt;       /* RTT in microsecond */
  uint64_t delivery_rate;  /* bytes per second */
};

/* TCP socket */
class TCPSocket : public Socket
{
protected:
    /* constructor used by accept() and SecureSocket() */
    TCPSocket( FileDescriptor && fd ) : Socket( std::move( fd ), AF_INET, SOCK_STREAM ) {}

public:
    TCPSocket() : Socket( AF_INET, SOCK_STREAM ) {}

    /* mark the socket as listening for incoming connections */
    void listen( const int backlog = 16 );

    /* accept a new incoming connection */
    TCPSocket accept( void );

    /* original destination of a DNAT connection */
    Address original_dest( void ) const;

    /* are there pending errors on a nonblocking socket? */
    void verify_no_errors() const;

    /* set the current congestion control algorithm */
    void set_congestion_control( const std::string & cc );

    /* get the current congestion control algorithm */
    std::string get_congestion_control() const;

    TCPInfo get_tcp_info() const;
};

#endif /* SOCKET_HH */
