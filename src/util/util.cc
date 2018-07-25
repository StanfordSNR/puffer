/* -*-mode:c++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */

#include "util.hh"

#include <cstdlib>
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>

#include <string>
#include <stdexcept>

using namespace std;

string safe_getenv(const string & key)
{
  const char * const value = getenv(key.c_str());
  if (not value) {
    throw runtime_error("missing environment variable: " + key);
  }
  return value;
}

string safe_getenv_or(const string & key, const string & def_val)
{
  const char * const value = getenv(key.c_str());
  if (not value) {
    return def_val;
  }
  return value;
}

string expand_user(const string & path)
{
  if (path.front() != '~') {
    return path;
  }

  string to_expand;
  string remain;
  auto slash_pos = path.find('/');
  if (slash_pos == string::npos) {
    to_expand = path;
  } else {
    to_expand = path.substr(0, slash_pos);
    remain = path.substr(slash_pos);
  }

  string expanded;
  if (to_expand == "~") {  // ~ or ~/*
    const char * home = getenv("HOME");
    if (home) {
      expanded = home;
    } else {
      struct passwd * pw = getpwuid(getuid());
      if (not pw) {
        throw runtime_error("invalid user id " + to_string(getuid()));
      }
      expanded = pw->pw_dir;
    }
  } else {  // ~USER or ~USER/*
    struct passwd * pw = getpwnam(to_expand.substr(1).c_str());
    if (not pw) {
      throw runtime_error("invalid username " + to_expand.substr(1));
    }
    expanded = pw->pw_dir;
  }

  return expanded + remain;
}
