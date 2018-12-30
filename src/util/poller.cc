/* -*-mode:c++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */

#include <algorithm>
#include <numeric>

#include "poller.hh"
#include "exception.hh"
#include "nb_secure_socket.hh"

using namespace std;
using namespace PollerShortNames;

Poller::Action::Action( NBSecureSocket & s_socket,
                        const PollDirection & s_direction,
                        const CallbackType & s_callback,
                        const std::function<bool(void)> & s_when_interested,
                        const std::function<void(void)> & s_fderror_callback,
                        const bool s_fail_poller )
  : fd( s_socket ), direction( s_direction ), callback(), when_interested(),
    fderror_callback( s_fderror_callback ), fail_poller ( s_fail_poller ),
    active( true )
{
  if ( direction == Out ) { /* write */
    callback =
      [s_callback, &s_socket] ()
      {
        Result retval;

        if ( s_socket.mode() == NBSecureSocket::Mode::connect and not s_socket.connected() ) {
          /* we're not connected yet, so let's continue */
          s_socket.continue_SSL_connect();
        }
        else if ( s_socket.mode() == NBSecureSocket::Mode::accept and not s_socket.accepted() ) {
          /* we've not accepted yet, so let's continue */
          s_socket.continue_SSL_accept();
        }
        else if ( s_socket.state() == NBSecureSocket::State::needs_ssl_write_to_write ) {
          s_socket.continue_SSL_write();
        }
        else if ( s_socket.state() == NBSecureSocket::State::ready ) {
          if ( not s_socket.something_to_write() ) {
            retval = s_callback();
          }

          s_socket.continue_SSL_write();
        }
        else if ( s_socket.state() == NBSecureSocket::State::needs_ssl_write_to_read ) {
          s_socket.continue_SSL_read();
        }
        else {
          cerr << "Warning: unexpected s_socket state " << static_cast<int>(s_socket.state())
               << "; register_write and continue" << endl;
          s_socket.register_write();
          return Result {};
        }

        return retval;
      };

    when_interested =
      [s_when_interested, &s_socket]()
      {
        return ( s_socket.state() == NBSecureSocket::State::needs_connect ) or
               ( s_socket.state() == NBSecureSocket::State::needs_ssl_write_to_connect ) or
               ( s_socket.state() == NBSecureSocket::State::needs_ssl_write_to_accept ) or
               ( s_socket.state() == NBSecureSocket::State::needs_ssl_write_to_write ) or
               ( s_socket.state() == NBSecureSocket::State::needs_ssl_write_to_read ) or
               ( s_socket.state() == NBSecureSocket::State::ready and s_when_interested() );
      };

  }
  else /* direction == In */ { /* read */
    callback =
      [s_callback, &s_socket] ()
      {
        if ( s_socket.mode() == NBSecureSocket::Mode::connect and not s_socket.connected() ) {
          /* we're not connected yet, so let's continue */
          s_socket.continue_SSL_connect();
        }
        else if ( s_socket.mode() == NBSecureSocket::Mode::accept and not s_socket.accepted() ) {
          /* we've not accepted yet, so let's continue */
          s_socket.continue_SSL_accept();
        }
        else if ( s_socket.state() == NBSecureSocket::State::needs_ssl_read_to_write ) {
          s_socket.continue_SSL_write();
        }
        else if ( s_socket.state() == NBSecureSocket::State::needs_ssl_read_to_read or
                  s_socket.state() == NBSecureSocket::State::ready ) {
          s_socket.continue_SSL_read();
        }
        else {
          cerr << "Warning: unexpected s_socket state " << static_cast<int>(s_socket.state())
               << "; register_read and continue" << endl;
          s_socket.register_read();
          return Result {};
        }

        if ( s_socket.something_to_read() ) {
          return s_callback();
        }

        return Result {};
      };

    when_interested =
      [s_when_interested, &s_socket]()
      {
        return ( s_socket.state() == NBSecureSocket::State::needs_connect ) or
               ( s_socket.state() == NBSecureSocket::State::needs_accept ) or
               ( s_socket.state() == NBSecureSocket::State::needs_ssl_read_to_connect ) or
               ( s_socket.state() == NBSecureSocket::State::needs_ssl_read_to_accept ) or
               ( s_socket.state() == NBSecureSocket::State::needs_ssl_read_to_write ) or
               ( s_socket.state() == NBSecureSocket::State::needs_ssl_read_to_read ) or
               ( s_socket.state() == NBSecureSocket::State::ready and s_when_interested() );
      };
  }
}

void Poller::add_action( Poller::Action action )
{
  /* the action won't be actually added until the next poll() function call.
     this allows us to call add_action inside the callback functions */
  action_add_queue_.push( action );
}

void Poller::remove_fd( const int fd_num )
{
  /* the fd won't be actually removed until the end of the current poll().
     this allows us to deregister a fd inside the callback functions */
  fds_to_remove_.emplace( fd_num );
}

unsigned int Poller::Action::service_count( void ) const
{
  return direction == Direction::In ? fd.read_count() : fd.write_count();
}

Poller::Result Poller::poll( const int timeout_ms )
{
  /* first, let's add all the actions that are waiting in the queue */
  while ( not action_add_queue_.empty() ) {
    Action & action = action_add_queue_.front();
    pollfds_.push_back( { action.fd.fd_num(), 0, 0 } );
    actions_.emplace_back( move( action ) );
    action_add_queue_.pop();
  }

  assert( pollfds_.size() == actions_.size() );

  if ( timeout_ms == 0 ) {
    throw runtime_error( "poll asked to busy-wait" );
  }

  /* tell poll whether we care about each fd */
  auto it_action = actions_.begin();
  auto it_pollfd = pollfds_.begin();

  for ( ; it_action != actions_.end() and it_pollfd != pollfds_.end()
        ; it_action++, it_pollfd++ ) {
    assert( it_pollfd->fd == it_action->fd.fd_num() );
    it_pollfd->events = (it_action->active and it_action->when_interested())
      ? it_action->direction : 0;

    /* don't poll in on fds that have had EOF */
    if ( it_action->direction == Direction::In
       and it_action->fd.eof() ) {
      it_pollfd->events = 0;
    }
  }

  /* Quit if no member in pollfds_ has a non-zero direction */
  if ( not accumulate( pollfds_.begin(), pollfds_.end(), false,
                       [] ( bool acc, pollfd x ) { return acc or x.events; } ) ) {
    return Result::Type::Exit;
  }

  if ( 0 == CheckSystemCall( "poll", ::poll( &pollfds_[ 0 ], pollfds_.size(), timeout_ms ) ) ) {
    return Result::Type::Timeout;
  }

  it_action = actions_.begin();
  it_pollfd = pollfds_.begin();

  for ( ; it_action != actions_.end() and it_pollfd != pollfds_.end()
        ; it_action++, it_pollfd++ ) {
    assert( it_pollfd->fd == it_action->fd.fd_num() );
    if ( it_pollfd->revents & (POLLERR | POLLHUP | POLLNVAL) ) {
      it_action->fderror_callback();
      remove_fd( it_pollfd->fd );
      continue;
    }

    if ( it_pollfd->revents & it_pollfd->events ) {
      /* we only want to call callback if revents includes
        the event we asked for */
      const auto count_before = it_action->service_count();

      try {
        auto result = it_action->callback();

        switch ( result.result ) {
        case ResultType::Exit:
          return Result( Result::Type::Exit, result.exit_status );

        case ResultType::Cancel:
          it_action->active = false;
          break;

        case ResultType::CancelAll:
          remove_fd( it_pollfd->fd );
          break;

        case ResultType::Continue:
          break;
        }
      } catch ( const exception & e ) {
        if ( it_action->fail_poller ) {
          /* throw only if the action is intended to fail the entire poller */
          throw;
        } else {
          /* simply remove the fd from poller and keep the poller running */
          print_exception( "Poller: error in callback", e );

          it_action->fderror_callback();
          remove_fd( it_pollfd->fd );
          continue;
        }
      }

      if ( count_before == it_action->service_count() ) {
        throw runtime_error( "Poller: busy wait detected: callback did not read/write fd" );
      }
    }
  }

  remove_actions( fds_to_remove_ );
  fds_to_remove_.clear();

  return Result::Type::Success;
}

void Poller::remove_actions( const set<int> & fd_nums )
{
  if ( fd_nums.size() == 0 ) {
    return;
  }

  auto it_action = actions_.begin();
  auto it_pollfd = pollfds_.begin();

  while ( it_action != actions_.end() and it_pollfd != pollfds_.end() ) {
    if ( fd_nums.count( it_pollfd->fd ) ) {
      it_action = actions_.erase( it_action );
      it_pollfd = pollfds_.erase( it_pollfd );
    }
    else {
      it_action++;
      it_pollfd++;
    }
  }
}
