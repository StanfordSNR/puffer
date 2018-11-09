/* -*-mode:c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef STRICT_CONVERSIONS_HH
#define STRICT_CONVERSIONS_HH

#include <cmath>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <sstream>

long int strict_atoi( const std::string & str, const int base = 10 );
double strict_atof( const std::string & str );
unsigned long int strict_atoui(const std::string & str, const int base = 10);
std::string double_to_string(const double input, const int precision);

/* Cast two integral types Source to Target.
 * Assert no precision is lost. */
template<typename Target, typename Source>
Target narrow_cast(const Source & s)
{
  static_assert(std::is_integral<Source>::value, "Source: integral required.");
  static_assert(std::is_integral<Target>::value, "Target: integral required.");

  Target t = static_cast<Target>(s);

  if (static_cast<Source>(t) != s) {
    throw std::runtime_error("narrow_cast: " + std::to_string(s) +
                             " is not equal to " + std::to_string(t));
  }

  return t;
}

/* Cast a floating-point type Float to an integral type Int.
 * Assert the lost precision is within a tolerance epsilon. */
template<typename Int, typename Float>
Int narrow_round(const Float & f)
{
  static_assert(std::is_floating_point<Float>::value,
                "Float: floating point required.");
  static_assert(std::is_integral<Int>::value, "Int: integral required.");

  Int i = static_cast<Int>(std::round(f));

  if (std::abs(f - i) > 0.001f) {
    throw std::runtime_error("narrow_round: " + std::to_string(f) +
                             " is not close to " + std::to_string(i));
  }

  return i;
}

#endif /* STRICT_CONVERSIONS_HH */
