#ifndef TIMESTAMP_HH
#define TIMESTAMP_HH

#include <cstdint>

const uint64_t MILLION = 1000 * 1000;
const uint64_t BILLION = 1000 * 1000 * 1000;

/* nanoseconds since epoch */
uint64_t timestamp_ns();

/* microseconds since epoch */
uint64_t timestamp_us();

/* milliseconds since epoch */
uint64_t timestamp_ms();

/* seconds since epoch */
uint64_t timestamp_s();

#endif /* TIMESTAMP_HH */
