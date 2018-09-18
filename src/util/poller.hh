/* -*-mode:c++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */

#ifndef POLLER_HH
#define POLLER_HH

#include <functional>
#include <vector>
#include <cassert>
#include <list>
#include <set>
#include <queue>
#include <poll.h>

#include "file_descriptor.hh"

class NBSecureSocket;

class Poller
{
public:
  struct Action
  {
    struct Result
    {
      enum class Type { Continue, Exit, Cancel, CancelAll } result;
      unsigned int exit_status;
      Result( const Type & s_result = Type::Continue, const unsigned int & s_status = EXIT_SUCCESS )
        : result( s_result ), exit_status( s_status ) {}
    };

    typedef std::function<Result(void)> CallbackType;

    FileDescriptor & fd;
    enum PollDirection : short { In = POLLIN, Out = POLLOUT } direction;
    CallbackType callback;
    std::function<bool(void)> when_interested;

    std::function<void(void)> fderror_callback;
    /* whether an error in this action's callback will fail the entire poller
     * set to false by default for NBSecureSocket and true for other fds */
    bool fail_poller;

    bool active;

    Action( FileDescriptor & s_fd,
            const PollDirection & s_direction,
            const CallbackType & s_callback,
            const std::function<bool(void)> & s_when_interested = [] () { return true; },
            const std::function<void(void)> & s_fderror_callback = [] () {},
            const bool s_fail_poller = true )
      : fd( s_fd ), direction( s_direction ), callback( s_callback ),
        when_interested( s_when_interested ),
        fderror_callback( s_fderror_callback ), fail_poller( s_fail_poller ),
        active( true ) {}

    Action( NBSecureSocket & s_socket,
            const PollDirection & s_direction,
            const CallbackType & s_callback,
            const std::function<bool(void)> & s_when_interested = [] () { return true; },
            const std::function<void(void)> & s_fderror_callback = [] () {},
            const bool s_fail_poller = false );

    unsigned int service_count( void ) const;
  };

private:
  std::queue<Action> action_add_queue_ {};
  std::list<Action> actions_ {};
  std::vector<pollfd> pollfds_ {};
  std::set<int> fds_to_remove_ {};

  /* remove all actions for file descriptors in `fd_nums` */
  void remove_actions( const std::set<int> & fd_nums );

public:
  struct Result
  {
    enum class Type { Success, Timeout, Exit } result;
    unsigned int exit_status;
    Result( const Type & s_result, const unsigned int & s_status = EXIT_SUCCESS )
      : result( s_result ), exit_status( s_status ) {}
  };

  Poller() {}

  void add_action( Action action );
  void remove_fd( const int fd_num );
  Result poll( const int timeout_ms );
};

namespace PollerShortNames {
  typedef Poller::Action::Result Result;
  typedef Poller::Action::Result::Type ResultType;
  typedef Poller::Action::PollDirection Direction;
}

#endif
