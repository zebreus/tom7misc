#ifndef _FCEULIB_CRC32_H
#define _FCEULIB_CRC32_H

#include <cstdint>

uint32_t CalcCRC32(uint32_t crc, const uint8_t *buf, uint32_t len);

#endif
