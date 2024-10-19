/*
Copyright (C) 2009-2010 DeSmuME team

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#include "emufile.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

using uint8 = uint8_t;
using uint16 = uint16_t;
using uint32 = uint32_t;
using uint64 = uint64_t;

using namespace std;

size_t EmuFile_MEMORY::fread(void *ptr, size_t bytes) {
  uint32 remain = len - pos;
  uint32 todo = std::min<uint32>(remain, (uint32)bytes);
  if (len == 0) {
    failbit = true;
    return 0;
  }
  if (todo <= 4) {
    uint8* src = buf() + pos;
    uint8* dst = (uint8*)ptr;
    for (size_t i = 0; i < todo; i++) *dst++ = *src++;
  } else {
    memcpy((void*)ptr, buf() + pos, todo);
  }
  pos += todo;
  if (todo < bytes) failbit = true;
  return todo;
}

size_t EmuFile_MEMORY_READONLY::fread(void *ptr, size_t bytes) {
  uint32 remain = len - pos;
  uint32 todo = std::min<uint32>(remain, (uint32)bytes);
  if (len == 0) {
    failbit = true;
    return 0;
  }
  if (todo <= 4) {
    const uint8 *src = buf() + pos;
    uint8 *dst = (uint8*)ptr;
    for (size_t i = 0; i < todo; i++) *dst++ = *src++;
  } else {
    memcpy((void*)ptr, buf() + pos, todo);
  }
  pos += todo;
  if (todo < bytes) failbit = true;
  return todo;
}

EmuFile_FILE::EmuFile_FILE(const string &fname, const string &mode)
  : fname(fname), mode(mode) {
  fp = fopen(fname.c_str(), mode.c_str());
  if (!fp) failbit = true;
}

void EmuFile::write32le(uint32* val) {
  write32le(*val);
}

void EmuFile::write32le(uint32 val) {
  uint8 s[4];
  s[0] = (uint8)val;
  s[1] = (uint8)(val >> 8);
  s[2] = (uint8)(val >> 16);
  s[3] = (uint8)(val >> 24);
  fwrite(s, 4);
}

bool EmuFile::read32le(int32_t* Bufo) {
  return read32le((uint32*)Bufo);
}

bool EmuFile::read32le(uint32* val) {
  uint8 buf[4];
  if (fread(&buf, 4) < 4) return false;

  *val =
    (uint32_t(buf[3]) << 24) |
    (uint32_t(buf[2]) << 16) |
    (uint32_t(buf[1]) << 8) |
    uint32_t(buf[0]);

  return true;
}

uint32 EmuFile::read32le() {
  uint32 ret = 0;
  read32le(&ret);
  return ret;
}

void EmuFile::write16le(uint16* val) {
  write16le(*val);
}

void EmuFile::write16le(uint16 val) {
  uint8 s[2];
  s[0] = (uint8)val;
  s[1] = (uint8)(val >> 8);
  fwrite(s, 2);
}

bool EmuFile::read16le(int16_t* Bufo) {
  return read16le((uint16*)Bufo);
}

bool EmuFile::read16le(uint16* val) {
  uint8_t buf[2];
  if (fread(&buf, 2) < 2) return false;
  *val = (uint16_t(buf[1]) << 8) | buf[0];
  return true;
}

uint16 EmuFile::read16le() {
  uint16 ret = 0;
  read16le(&ret);
  return ret;
}

void EmuFile::write8le(uint8* val) {
  write8le(*val);
}

void EmuFile::write8le(uint8 val) {
  fwrite(&val, 1);
}

bool EmuFile::read8le(uint8* val) {
  return fread(val, 1) != 0;
}

uint8 EmuFile::read8le() {
  uint8 temp;
  fread(&temp, 1);
  return temp;
}
