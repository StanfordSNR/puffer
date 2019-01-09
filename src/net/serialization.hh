/* -*-mode:c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef SERIALIZATION_HH
#define SERIALIZATION_HH

#include <string>
#include <cstdint>

std::string put_field(const uint16_t n);
std::string put_field(const uint32_t n);
std::string put_field(const uint64_t n);
uint16_t get_uint16(const char * data);
uint32_t get_uint32(const char * data);
uint64_t get_uint64(const char * data);

#endif /* SERIALIZATION_HH */
