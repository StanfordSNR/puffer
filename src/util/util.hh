/* -*-mode:c++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */

#ifndef UTIL_HH
#define UTIL_HH

#include <string>

std::string safe_getenv(const std::string & key);

std::string safe_getenv_or(const std::string & key, const std::string & def_val);

/* expand leading tilde */
std::string expand_user(const std::string & path);

#endif /* UTIL_HH */
