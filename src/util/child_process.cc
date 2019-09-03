/* -*-mode:c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <sys/wait.h>
#include <cassert>
#include <cstdlib>
#include <sys/syscall.h>
#include <string>
#include <cstring>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "child_process.hh"
#include "system_runner.hh"
#include "exception.hh"
#include "signalfd.hh"

using namespace std;
using namespace PollerShortNames;

template <typename T> void zero( T & x ) { memset( &x, 0, sizeof( x ) ); }

int do_fork()
{
    /* Verify that process is single-threaded before forking */
    {
        struct stat my_stat;
        CheckSystemCall( "stat", stat( "/proc/self/task", &my_stat ) );

        if ( my_stat.st_nlink != 3 ) {
            throw runtime_error( "ChildProcess constructed in multi-threaded program" );
        }
    }

    return CheckSystemCall( "fork", fork() );
}

/* start up a child process running the supplied lambda */
/* the return value of the lambda is the child's exit status */
ChildProcess::ChildProcess( const string & name,
                            function<int()> && child_procedure,
                            const int termination_signal )
    : name_( name ),
      pid_( do_fork() ),
      running_( true ),
      terminated_( false ),
      exit_status_(),
      died_on_signal_( false ),
      graceful_termination_signal_( termination_signal ),
      moved_away_( false )
{
    if ( pid_ == 0 ) { /* child */
        try {
            SignalMask( {} ).set_as_mask();
            _exit( child_procedure() );
        } catch ( const exception & e ) {
            print_exception( name_.c_str(), e );
            _exit( EXIT_FAILURE );
        }
    }
}

/* is process in a waitable state? */
bool ChildProcess::waitable( void ) const
{
    assert( !moved_away_ );
    assert( !terminated_ );

    siginfo_t infop;
    zero( infop );
    CheckSystemCall( "waitid", waitid( P_PID, pid_, &infop,
                                       WEXITED | WSTOPPED | WCONTINUED | WNOHANG | WNOWAIT ) );

    if ( infop.si_pid == 0 ) {
        return false;
    } else if ( infop.si_pid == pid_ ) {
        return true;
    } else {
        throw runtime_error( "waitid: unexpected value in siginfo_t si_pid field (not 0 or pid)" );
    }
}

/* wait for process to change state */
void ChildProcess::wait( const bool nonblocking )
{
    assert( !moved_away_ );
    assert( !terminated_ );

    siginfo_t infop;
    zero( infop );
    CheckSystemCall( "waitid", waitid( P_PID, pid_, &infop,
                                       WEXITED | WSTOPPED | WCONTINUED | (nonblocking ? WNOHANG : 0) ) );

    if ( nonblocking and (infop.si_pid == 0) ) {
        throw runtime_error( "nonblocking wait: process was not waitable" );
    }

    if ( infop.si_pid != pid_ ) {
        throw runtime_error( "waitid: unexpected value in siginfo_t si_pid field" );
    }

    if ( infop.si_signo != SIGCHLD ) {
        throw runtime_error( "waitid: unexpected value in siginfo_t si_signo field (not SIGCHLD)" );
    }

    /* how did the process change state? */
    switch ( infop.si_code ) {
    case CLD_EXITED:
        terminated_ = true;
        exit_status_ = infop.si_status;
        break;
    case CLD_KILLED:
    case CLD_DUMPED:
        terminated_ = true;
        exit_status_ = infop.si_status;
        died_on_signal_ = true;
        break;
    case CLD_STOPPED:
        running_ = false;
        break;
    case CLD_CONTINUED:
        running_ = true;
        break;
    default:
        throw runtime_error( "waitid: unexpected siginfo_t si_code" );
    }
}

/* if child process was suspended, resume it */
void ChildProcess::resume( void )
{
    assert( !moved_away_ );

    if ( !running_ ) {
        signal( SIGCONT );
    }
}

/* send a signal to the child process */
void ChildProcess::signal( const int sig )
{
    assert( !moved_away_ );

    if ( !terminated_ ) {
        CheckSystemCall( "kill", kill( pid_, sig ) );
    }
}

ChildProcess::~ChildProcess()
{
    if ( moved_away_ ) { return; }

    try {
        while ( !terminated_ ) {
            resume();
            signal( graceful_termination_signal_ );
            wait();
        }
    } catch ( const exception & e ) {
        print_exception( name_.c_str(), e );
    }
}

/* move constructor */
ChildProcess::ChildProcess( ChildProcess && other )
    : name_( other.name_ ),
      pid_( other.pid_ ),
      running_( other.running_ ),
      terminated_( other.terminated_ ),
      exit_status_( other.exit_status_ ),
      died_on_signal_( other.died_on_signal_ ),
      graceful_termination_signal_( other.graceful_termination_signal_ ),
      moved_away_( other.moved_away_ )
{
    assert( !other.moved_away_ );

    other.moved_away_ = true;
}

void ChildProcess::throw_exception( void ) const
{
    throw runtime_error( "`" + name() + "': process "
                         + (died_on_signal()
                            ? string("died on signal ")
                            : string("exited with failure status "))
                         + to_string( exit_status() ) );
}

ProcessManager::ProcessManager()
  : child_processes_(),
    callbacks_(),
    error_callbacks_(),
    poller_(),
    signals_({ SIGCHLD, SIGABRT, SIGHUP, SIGINT, SIGQUIT, SIGTERM }),
    signal_fd_(signals_)
{
  /* use signal_fd_ to read signals */
  signals_.set_as_mask();

  /* poller listens on signal_fd_ for signals */
  poller_.add_action(
    Poller::Action(signal_fd_.fd(), Direction::In,
      [this]() {
        return handle_signal(signal_fd_.read_signal());
      }
    )
  );
}

pid_t ProcessManager::run_as_child(const string & program,
                                   const vector<string> & prog_args,
                                   const callback_t & callback,
                                   const callback_t & error_callback,
                                   const std::string & log_path)
{
  auto child = ChildProcess(program,
    [&program, &prog_args, &log_path]() {
      /* references won't be dangling as they will be used immediately */
      if (not log_path.empty()) {
        /* redirect stdout and stderr to log_path */
        FileDescriptor fd(CheckSystemCall("open (" + log_path + ")",
            open(log_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644)));

        CheckSystemCall("dup2", dup2(fd.fd_num(), STDOUT_FILENO));
        CheckSystemCall("dup2", dup2(fd.fd_num(), STDERR_FILENO));

        fd.close();
      }

      return ezexec(program, prog_args);
    }
  );

  pid_t pid = child.pid();
  child_processes_.emplace(pid, move(child));

  if (callback) {
    callbacks_.emplace(pid, callback);
  }

  if (error_callback) {
    error_callbacks_.emplace(pid, error_callback);
  }

  cerr << "[" + to_string(pid) + "] " + command_str(prog_args) + "\n";

  return pid;
}

int ProcessManager::wait()
{
  while (not child_processes_.empty()) {
    auto ret = poller_.poll(-1);
    if (ret.result != Poller::Result::Type::Success) {
      return ret.exit_status;
    }
  }

  return EXIT_SUCCESS;
}

int ProcessManager::loop()
{
  for (;;) {
    auto ret = poller_.poll(-1);
    if (ret.result != Poller::Result::Type::Success) {
      return ret.exit_status;
    }
  }
}

int ProcessManager::run(const string & program,
                        const vector<string> & prog_args,
                        const callback_t & callback)
{
  run_as_child(program, prog_args, callback);
  return wait();
}

Result ProcessManager::handle_signal(const signalfd_siginfo & sig)
{
  switch (sig.ssi_signo) {
  /* if an exception is thrown from the child, throw another exception
   * to notify ProcessManager's parent process if there is */
  case SIGCHLD:
    if (child_processes_.empty()) {
      cerr << "ProcessManager: received SIGCHLD without any children" << endl;
      break;
    }

    for (auto it = child_processes_.begin(); it != child_processes_.end();) {
      ChildProcess & child = it->second;

      if (not child.waitable()) {
        ++it;
      } else {
        child.wait(true);

        if (child.terminated()) {
          if (child.exit_status() != 0) {
            /* call the corresponding callback function if it exists */
            const auto & callback_it = error_callbacks_.find(it->first);
            if (callback_it != error_callbacks_.end()) {
              callback_it->second(it->first);
            } else {
              child_processes_.clear();
              throw runtime_error("ProcessManager: PID " + to_string(it->first) +
                                " exits abnormally");
            }
          }

          /* call the corresponding callback function if it exists */
          const auto & callback_it = callbacks_.find(it->first);
          if (callback_it != callbacks_.end()) {
            callback_it->second(it->first);
          }

          it = child_processes_.erase(it);
        } else {
          if (not child.running()) {
            /* call the corresponding callback function if it exists */
            const auto & callback_it = error_callbacks_.find(it->first);
            if (callback_it != error_callbacks_.end()) {
              callback_it->second(it->first);
            } else {
              child_processes_.clear();
              throw runtime_error("ProcessManager: PID " + to_string(it->first) +
                                " is not running");
            }
          }

          ++it;
        }
      }
    }

    break;
  /* handle other exceptions by destroying children and return failure */
  case SIGABRT:
  case SIGHUP:
  case SIGINT:
  case SIGQUIT:
  case SIGTERM:
    cerr << "ProcessManager: interrupted by signal " +
            to_string(sig.ssi_signo) + "\n";
    child_processes_.clear();
    return {ResultType::Exit, EXIT_FAILURE};
  default:
    cerr << "ProcessManager: unknown signal " +
            to_string(sig.ssi_signo) + "\n";
    child_processes_.clear();
    return {ResultType::Exit, EXIT_FAILURE};
  }

  return {};  /* success and continue: {ResultType::Continue, EXIT_SUCCESS}; */
}
