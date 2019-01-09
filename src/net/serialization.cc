/* -*-mode:c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include "serialization.hh"

using namespace std;

string put_field(const uint16_t n)
{
  const uint16_t network_order = htobe16(n);
  return string(reinterpret_cast<const char *>(&network_order),
                sizeof(network_order));
}

string put_field(const uint32_t n)
{
  const uint32_t network_order = htobe32(n);
  return string(reinterpret_cast<const char *>(&network_order),
                sizeof(network_order));
}

string put_field(const uint64_t n)
{
  const uint64_t network_order = htobe64(n);
  return string(reinterpret_cast<const char *>(&network_order),
                sizeof(network_order));
}

uint16_t get_uint16(const char * data)
{
  return be16toh(*reinterpret_cast<const uint16_t *>(data));
}

uint32_t get_uint32(const char * data)
{
  return be32toh(*reinterpret_cast<const uint32_t *>(data));
}

uint64_t get_uint64(const char * data)
{
  return be64toh(*reinterpret_cast<const uint64_t *>(data));
}
