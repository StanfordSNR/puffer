/* -*-mode:c++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */

#include "system_runner.hh"

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sstream>
#include <array>
#include <optional>

#include "child_process.hh"
#include "exception.hh"
#include "file_descriptor.hh"
#include "pipe.hh"

using namespace std;

int ezexec(const string & filename, const vector<string> & args,
           const vector<string> & env, const bool use_environ,
           const bool path_search)
{
  if (args.empty()) {
    throw runtime_error("ezexec: empty args");
  }

  if (geteuid() == 0 or getegid() == 0) {
    if (environ) {
      throw runtime_error("BUG: root's env not cleared");
    }

    if (path_search) {
      throw runtime_error("BUG: root should not search PATH");
    }
  }

  /* copy the arguments to mutable structures */
  vector<char *> argv;
  vector<vector<char>> argv_data;

  for (auto & x : args) {
    vector<char> new_str;
    for (auto & ch : x) {
      new_str.push_back(ch);
    }
    new_str.push_back(0); /* null-terminate */

    argv_data.push_back(new_str);
  }

  for (auto & x : argv_data) {
    argv.push_back(&x[0]);
  }

  argv.push_back(0); /* null-terminate */

  /* copy the env variables to mutable structures */
  vector<char *> envp;
  vector<vector<char>> envp_data;

  if (not use_environ) {
    for (auto & x : env) {
      vector<char> new_str;
      for (auto & ch : x) {
        new_str.push_back(ch);
      }
      new_str.push_back(0); /* null-terminate */

      envp_data.push_back(new_str);
    }

    for (auto & x : envp_data) {
      envp.push_back(&x[0]);
    }

    envp.push_back(0); /* null-terminate */
  }

  return (path_search ? execvpe : execve)(filename.c_str(), &argv[0],
                                          use_environ ? environ : &envp[0]);
}

pair<string, string> run(
    const string & filename, const vector<string> & args,
    const bool read_stdout_until_eof, const bool read_stderr_until_eof,
    const vector<string> & env, const bool use_environ, const bool path_search)
{
  string stdout_output, stderr_output;

  optional<pair<FileDescriptor, FileDescriptor>> stdout_pipe;
  if (read_stdout_until_eof) {
    stdout_pipe = make_pipe();
  }

  optional<pair<FileDescriptor, FileDescriptor>> stderr_pipe;
  if (read_stderr_until_eof) {
    stderr_pipe = make_pipe();
  }

  ChildProcess command_process(args[0],
    [&filename, &args, &env, use_environ, path_search,
     &stdout_pipe, &stderr_pipe]()
    {
      if (stdout_pipe) {
        stdout_pipe->first.close();
        CheckSystemCall("dup2", dup2(stdout_pipe->second.fd_num(),
                                     STDOUT_FILENO));
      }

      if (stderr_pipe) {
        stderr_pipe->first.close();
        CheckSystemCall("dup2", dup2(stderr_pipe->second.fd_num(),
                                     STDERR_FILENO));
      }

      return ezexec(filename, args, env, use_environ, path_search);
    }
 );

  if (stdout_pipe) {
    stdout_pipe->second.close();
    while (not stdout_pipe->first.eof()) {
      stdout_output.append(stdout_pipe->first.read());
    }
  }

  if (stderr_pipe) {
    stderr_pipe->second.close();
    while (not stderr_pipe->first.eof()) {
      stderr_output.append(stderr_pipe->first.read());
    }
  }

  while (!command_process.terminated()) {
    command_process.wait();
  }

  if (command_process.exit_status() != 0) {
    command_process.throw_exception();
  }

  return {stdout_output, stderr_output};
}

string command_str(const vector<string> & command,
                   const vector<string> & env)
{
  ostringstream oss;

  for (const auto & e : env) { oss << e << " "; }
  for (const auto & c : command) { oss << c << " "; }

  return oss.str();
}

std::string command_str(const int argc, char * argv[])
{
  ostringstream oss;

  for (int i = 0; i < argc; i++) {
    oss << argv[i];

    if (i != argc - 1)  {
      oss << " ";
    }
  }

  return oss.str();
}
