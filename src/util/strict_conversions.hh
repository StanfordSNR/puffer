/* -*-mode:c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef STRICT_CONVERSIONS_HH
#define STRICT_CONVERSIONS_HH

#include <stdexcept>
#include <string>

long int strict_atoi( const std::string & str, const int base = 10 );
double strict_atof( const std::string & str );
unsigned long int strict_atoui(const std::string & str, const int base = 10);

template<class Target, class Source>
Target narrow_cast(const Source & v)
{
  auto r = static_cast<Target>(v);

  if (static_cast<Source>(r) != v) {
    throw std::runtime_error("narrow_cast failed");
  }

  return r;
}

#endif /* STRICT_CONVERSIONS_HH */
