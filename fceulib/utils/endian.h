#ifndef _FCEULIB_ENDIAN_H
#define _FCEULIB_ENDIAN_H

#include <cstdlib>
#include <iosfwd>
#include <cstdio>

#include "../types.h"

class EmuFile;

int write16le(uint16 b, FILE *fp);
int write32le(uint32 b, FILE *fp);
int write32le(uint32 b, std::ostream *os);

int read16le(uint16 *buf, std::istream *is);
int read32le(uint32 *buf, std::istream *is);
int read32le(uint32 *buf, FILE *fp);

void FlipByteOrder(uint8 *src, uint32 count);

int write16le(uint16 b, EmuFile *os);
int write32le(uint32 b, EmuFile *os);

int read16le(uint16 *buf, EmuFile *is);
inline int read16le(int16 *buf, EmuFile *is) {
  return read16le((uint16*)buf,is);
}
int read32le(uint32 *buf, EmuFile *is);
inline int read32le(int32 *buf, EmuFile *is) {
  return read32le((uint32*)buf,is);
}

#endif


