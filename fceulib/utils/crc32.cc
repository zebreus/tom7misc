// This file only: Based on code by NetworkDLS.
//    (https://github.com/NTDLS/NSWFL)
// See bottom of file for its license (MIT).

#include "crc32.h"

#include <cstdint>
#include <cstring>

static uint32_t Reflect(uint32_t iReflect, const char cChar) {
  uint32_t iValue = 0;

  // Swap bit 0 for bit 7, bit 1 For bit 6, etc....
  for (int iPos = 1; iPos < (cChar + 1); iPos++) {
    if (iReflect & 1) {
      iValue |= (1 << (cChar - iPos));
    }
    iReflect >>= 1;
  }

  return iValue;
}

namespace {
struct CRCTable {
  // The official polynomial used by PKZip, WinZip and Ethernet.
  static constexpr uint32_t POLYNOMIAL = 0x04C11DB7;
  CRCTable() {
    memset(&this->table, 0, sizeof(this->table));

    // 256 values representing ASCII character codes.
    for (int b = 0; b <= 0xFF; b++) {
      uint32_t x = Reflect(b, 8) << 24;

      for (int bit = 0; bit < 8; bit++) {
        x = (x << 1) ^ ((x & (1 << 31)) ? POLYNOMIAL : 0);
      }

      this->table[b] = Reflect(x, 32);
    }
  }

  uint32_t table[256] = {};
};
}  // namespace

// Calculates the CRC32 by looping through each of the bytes in sData.
// This is comparable to crc32() in zlib.
uint32_t CalcCRC32(uint32_t init,
                   const uint8_t *data, uint32_t len) {
  static const CRCTable *crc_table = new CRCTable;
  uint32_t ret = ~init;
  while (len--) {
    ret = (ret >> 8) ^ crc_table->table[(ret & 0xFF) ^ *data++];
  }
  return ~ret;
}

///////////////////////////////////////////////////////////////////////////
//  Copyright © NetworkDLS 2023, All rights reserved
//
// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
///////////////////////////////////////////////////////////////////////////

/*
MIT License

Copyright (c) 2024 Josh Patterson

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/
