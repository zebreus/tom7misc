
#ifndef _ESCAPE_BYTES_H
#define _ESCAPE_BYTES_H

#include <cstdint>
#include <string>

#include "packet-parser.h"

// For portability, we avoid assuming any endianness in escape file
// formats, instead working only with byte vectors (usually stored in
// strings).
//
// These functions used to be part of escape's escape-util.h and also
// made their way into cc-lib and elsewhere. They had really confusing
// names like "sizes", "shint", and "shout", which were based on weird
// puns.
//
// This is a header-only replacement so that the level and solution
// code (which includes reading/writing the serialized format) can be
// independent from ./escape-util.cc, which causes some problems for
// embedding these in the server module (some symbols clash with
// cc-lib's utils). (This might be avoidable now since I've been
// trying to make cc-lib util and escape-util coexist.)

// Convert i into a four-byte string (big-endian).
inline std::string BigEndian32(int i) {
  std::string s = "    ";
  s[0] = 255 & (i >> 24);
  s[1] = 255 & (i >> 16);
  s[2] = 255 & (i >> 8);
  s[3] = 255 &  i;
  return s;
}

// Read a four-byte string at idx in the string (no bounds checking).
// Advances the index by four.
inline uint32_t ReadBigEndian32(const std::string &s, unsigned int &idx) {
  PacketParser pp(s);
  pp.Skip(idx);
  uint32_t u = pp.W32();
  idx += 4;
  return u;
}


#endif
