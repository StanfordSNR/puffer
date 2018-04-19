/* -*-mode:c++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */

#ifndef SYSTEM_RUNNER_HH
#define SYSTEM_RUNNER_HH

#include <vector>
#include <string>
#include <utility>

int ezexec(const std::string & filename,
           const std::vector<std::string> & args,
           const std::vector<std::string> & env = {},
           const bool use_environ = true,
           const bool path_search = true);

/* run the command "args" till the end blockingly
 * return the output to <stdout, stderr> if the corresponding bools are true */
std::pair<std::string, std::string> run(
           const std::string & filename,
           const std::vector<std::string> & args,
           const bool read_stdout_until_eof = false,
           const bool read_stderr_until_eof = false,
           const std::vector<std::string> & env = {},
           const bool use_environ = true,
           const bool path_search = true);

std::string command_str(const std::vector<std::string> & command,
                        const std::vector<std::string> & env = {});

std::string command_str(const int argc, char * argv[]);

#endif /* SYSTEM_RUNNER_HH */
