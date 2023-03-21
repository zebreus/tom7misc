
#include <bit>
#include <cstdint>
#include <cstdio>
#include <algorithm>
#include <cmath>

#include "image.h"
#include "color-util.h"

#include "half.h"

using uint8 = uint8_t;

using half_float::half;
using namespace half_float::literal;

static constexpr inline uint32_t GetU32(float f) {
  return std::bit_cast<uint32_t, float>(f);
}

static constexpr inline float GetFloat(uint32_t u) {
  return std::bit_cast<float, uint32_t>(u);
}

static inline NextAfter32(uint32_t pos) {
  // Zero comes immediately after -0.
  if (pos == 0x80000000) return 0x0000;
  else if (pos > 0x80000000) return pos - 1;
  else return pos + 1;
}

static constexpr inline uint16_t GetU16(half_float::half h) {
  return std::bit_cast<uint16_t, half_float::half>(h);
}

static constexpr inline half_float::half GetHalf(uint16_t u) {
  return std::bit_cast<half_float::half, uint16_t>(u);
}

static inline NextAfter16(uint16_t pos) {
  // Zero comes immediately after -0.
  if (pos == 0x8000) return 0x0000;
  else if (pos > 0x8000) return pos - 1;
  else return pos + 1;
}


static void Test32() {
  uint32_t last_ou = 0x0001;
  for (uint32_t u = GetU32(-256.0f);
       u != GetU32(512.0f);
       u = NextAfter32(u)) {
    float in = GetFloat(u);
    float out = in - 127.5f + 3221225472.0f - 3221225472.0f;
    uint32_t ou = GetU32(out);
    if (ou != last_ou) {
      printf("%08x -> %08x (%.11g -> %.11g)\n", u, ou, in, out);
      last_ou = ou;
    }
  }
}

static void Test16() {
  uint16_t last_ou = 0x0001;
  for (uint16_t u = GetU16(-256.0_h);
       u != GetU16(256.0_h);
       u = NextAfter16(u)) {
    half in = GetHalf(u);
    half out = (in - 0.5_h) + 2048.0_h - 2048.0_h;
    uint16_t ou = GetU16(out);
    if (ou != last_ou) {
      printf("%04x -> %04x (%.11g -> %.11g)\n", u, ou, (float)in, (float)out);
      last_ou = ou;
    }
  }
}

static void HowMany16() {
  int64_t num = 0;
  for (uint16_t u = GetU16(0.0_h);
       u != GetU16(256.0_h);
       u = NextAfter16(u)) {
    num++;
  }
  printf("There are %lld halfs from 0 to 256.\n", num);
}

inline float fu8_add(float a, float b) {
  float x = a + b;
  x -= x - 127.5f + 3221225472.0f - 3221225472.0f;
  return x;
}

template<class F>
static void All(int size, F f) {
  for (int y = 0; y < size; y++) {
    for (int x = 0; x < size; x++) {
      float fy = (y * 256.0f) / (float)size;
      float fx = (x * 256.0f) / (float)size;

      float fs = fu8_add(fx, fy);

      uint8 y8 = (uint8)fy;
      uint8 x8 = (uint8)fx;
      uint8 s8 = x8 + y8;

      float err = fs - (float)s8;
      f(x, y, err);
    }
  }
}

static void AddImage(int size) {
  ImageRGBA img(size, size);
  img.Clear32(0x000000FF);

  // First, get max error.
  float lo = 0, hi = 0;
  All(size, [&lo, &hi](int x_, int y_, float err) {
      if (err < 0.0)
        lo = std::min(lo, err);
      else
        hi = std::max(hi, err);
    });

  // Now again, but draw
  All(size, [lo, hi, &img](int x, int y, float err) {
      if (err < 0.0) {
        float v = 0.25 + (err / lo) * 0.75;
        img.SetPixel(x, y, 255 * v, 0, 0, 255);
      } else if (err > 0.0) {
        float v = 0.25 + (err / hi) * 0.75;
        img.SetPixel(x, y, 0, 255 * v, 0, 255);
      } else {
        img.SetPixel32(x, y, 0xFFFFFFFF);
      }
    });

  img.Save("add-error.png");
}

int main(int argc, char **argv) {
  printf("16:\n");
  // Test16();

  // HowMany16();
  AddImage(8192);

  // printf("32:\n");
  // Test32();

  return 0;
}
