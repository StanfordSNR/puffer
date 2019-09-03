/* -*-mode:c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef CHILD_PROCESS_HH
#define CHILD_PROCESS_HH

#include <functional>
#include <unistd.h>
#include <cassert>
#include <csignal>

#include <unordered_map>
#include <vector>
#include <string>
#include "signalfd.hh"
#include "poller.hh"

/* object-oriented wrapper for handling Unix child processes */

class ChildProcess
{
private:
    std::string name_;
    pid_t pid_;
    bool running_, terminated_;
    int exit_status_;
    bool died_on_signal_;
    int graceful_termination_signal_;

    bool moved_away_;

public:
    ChildProcess( const std::string & name,
                  std::function<int()> && child_procedure,
                  const int termination_signal = SIGHUP );

    bool waitable( void ) const; /* is process in a waitable state? */
    void wait( const bool nonblocking = false ); /* wait for process to change state */
    void signal( const int sig ); /* send signal */
    void resume( void ); /* send SIGCONT */

    const std::string & name( void ) const { assert( not moved_away_ ); return name_; }
    pid_t pid( void ) const { assert( not moved_away_ ); return pid_; }
    bool running( void ) const { assert( not moved_away_ ); return running_; }
    bool terminated( void ) const { assert( not moved_away_ ); return terminated_; }

    /* Return exit status or signal that killed process */
    bool died_on_signal( void ) const { assert( not moved_away_ ); assert( terminated_ ); return died_on_signal_; }
    int exit_status( void ) const { assert( not moved_away_ ); assert( terminated_ ); return exit_status_; }
    void throw_exception( void ) const;

    ~ChildProcess();

    /* ban copying */
    ChildProcess( const ChildProcess & other ) = delete;
    ChildProcess & operator=( const ChildProcess & other ) = delete;

    /* allow move constructor */
    ChildProcess( ChildProcess && other );

    /* ... but not move assignment operator */
    ChildProcess & operator=( ChildProcess && other ) = delete;
};

/* class for managing child processes; this class handles signals properly */
class ProcessManager
{
public:
  using callback_t = std::function<void(const pid_t &)>;

  ProcessManager();

  /* run the program as a child process
   * call the callback function if the child exits with 0 */
  pid_t run_as_child(const std::string & program,
                     const std::vector<std::string> & prog_args,
                     const callback_t & callback = {},
                     const callback_t & error_callback = {},
                     const std::string & log_path = "");

  /* return when all the child processes exit */
  int wait();

  /* loop forever even if all the child processes exit and return only on error
   * especially helpful when the exposed poller also polls on other events */
  int loop();

  /* a helper function that calls run_as_child() and wait() */
  int run(const std::string & program,
          const std::vector<std::string> & prog_args,
          const callback_t & callback = {});

  /* expose the poller */
  Poller & poller() { return poller_; }

private:
  std::unordered_map<pid_t, ChildProcess> child_processes_;
  std::unordered_map<pid_t, callback_t> callbacks_;
  std::unordered_map<pid_t, callback_t> error_callbacks_;

  Poller poller_;

  SignalMask signals_;
  SignalFD signal_fd_;

  PollerShortNames::Result handle_signal(const signalfd_siginfo & sig);
};

#endif /* CHILD_PROCESS_HH */
