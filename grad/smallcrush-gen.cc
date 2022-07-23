
#include <vector>
#include <cstdint>
#include <bit>

#ifdef __cplusplus
extern "C" {
#endif

#include "usoft.h"
#include "unif01.h"
#include "bbattery.h"

#ifdef __cplusplus
}
#endif


#include "util.h"

#include "base/logging.h"
#include "ansi.h"
#include "color-util.h"
#include "image.h"
#include "arcfour.h"
#include "timer.h"

static constexpr int TARGET_FLOATS = 51320000;

using int64 = int64_t;

template<std::size_t N, class F, std::size_t START = 0>
inline void repeat(const F &f) {
  if constexpr (N == 0) {
    return;
  } else {
    f(START);
    repeat<N - 1, F, START + 1>(f);
  }
}

/*
// cycles 5+11; only 16 pairs have non-perfect avalanche
 {9, 0, 15, 10, 11, 7, 13, 1, 12, 5, 6, 3, 2, 14, 8, 4,  },
 {11, 14, 13, 4, 2, 8, 7, 1, 10, 6, 0, 5, 15, 12, 3, 9,  },
 {13, 14, 1, 2, 7, 4, 11, 8, 9, 3, 15, 5, 0, 10, 6, 12,  },
 {3, 6, 4, 8, 9, 12, 7, 14, 10, 15, 2, 1, 0, 5, 11, 13,  },
 {3, 15, 9, 10, 7, 11, 4, 2, 0, 6, 5, 12, 1, 8, 13, 14,  },
 {10, 8, 9, 2, 15, 1, 12, 4, 6, 14, 5, 7, 3, 11, 0, 13,  },
 {5, 3, 7, 13, 15, 10, 4, 14, 0, 6, 11, 8, 9, 12, 1, 2,  },
 {13, 7, 14, 11, 1, 2, 4, 8, 3, 5, 0, 9, 10, 15, 6, 12,  },

 // cycle 16; error: 16
 {11, 13, 7, 14, 1, 8, 2, 4, 0, 5, 6, 15, 3, 9, 10, 12,  },
 {4, 13, 5, 12, 1, 11, 9, 10, 2, 7, 15, 6, 8, 14, 3, 0,  },
 {10, 6, 11, 13, 0, 3, 7, 4, 12, 5, 14, 8, 9, 15, 2, 1,  },
 {14, 2, 4, 7, 8, 11, 13, 1, 15, 10, 3, 9, 5, 12, 6, 0,  },
 {10, 9, 14, 4, 12, 0, 8, 2, 15, 5, 11, 13, 6, 3, 1, 7,  },
 {3, 5, 9, 15, 0, 6, 12, 10, 4, 14, 1, 8, 13, 11, 7, 2,  },
 {4, 8, 7, 1, 14, 13, 2, 11, 0, 6, 12, 5, 9, 15, 10, 3,  },
 {6, 13, 5, 7, 10, 4, 9, 14, 3, 11, 15, 1, 0, 2, 12, 8,  },

 // cycles 13+3; error: 16
 {13, 12, 1, 0, 7, 15, 4, 6, 14, 5, 8, 3, 11, 9, 2, 10,  },
 {14, 8, 11, 13, 2, 1, 7, 4, 10, 3, 12, 15, 0, 9, 6, 5,  },
 {1, 2, 8, 4, 15, 12, 9, 5, 7, 11, 14, 13, 6, 10, 0, 3,  },
 {15, 9, 12, 10, 5, 3, 0, 6, 2, 11, 1, 7, 8, 14, 4, 13,  },
 {6, 10, 7, 11, 5, 9, 1, 8, 12, 15, 4, 13, 0, 3, 2, 14,  },
 {13, 4, 11, 2, 12, 15, 0, 3, 1, 8, 7, 14, 5, 6, 9, 10,  },
 {1, 11, 13, 8, 6, 12, 10, 9, 7, 14, 4, 2, 3, 15, 0, 5,  },
 {8, 13, 5, 15, 2, 4, 3, 9, 11, 14, 0, 6, 1, 7, 10, 12,  },
*/

static constexpr uint8_t CONST[16] = {
  // (note: from perms above)
  13, 7, 14, 11, 1, 2, 4, 8, 3, 5, 0, 9, 10, 15, 6, 12,
};


static constexpr uint8_t PERMS[16][16] = {
  // full 16
 {11, 13, 7, 14, 1, 8, 2, 4, 0, 5, 6, 15, 3, 9, 10, 12,  },
 {4, 13, 5, 12, 1, 11, 9, 10, 2, 7, 15, 6, 8, 14, 3, 0,  },
 {10, 6, 11, 13, 0, 3, 7, 4, 12, 5, 14, 8, 9, 15, 2, 1,  },
 {14, 2, 4, 7, 8, 11, 13, 1, 15, 10, 3, 9, 5, 12, 6, 0,  },
 {10, 9, 14, 4, 12, 0, 8, 2, 15, 5, 11, 13, 6, 3, 1, 7,  },
 {3, 5, 9, 15, 0, 6, 12, 10, 4, 14, 1, 8, 13, 11, 7, 2,  },
 {4, 8, 7, 1, 14, 13, 2, 11, 0, 6, 12, 5, 9, 15, 10, 3,  },
 {6, 13, 5, 7, 10, 4, 9, 14, 3, 11, 15, 1, 0, 2, 12, 8,  },
 // 5+11
 {9, 0, 15, 10, 11, 7, 13, 1, 12, 5, 6, 3, 2, 14, 8, 4,  },
 {11, 14, 13, 4, 2, 8, 7, 1, 10, 6, 0, 5, 15, 12, 3, 9,  },
 {13, 14, 1, 2, 7, 4, 11, 8, 9, 3, 15, 5, 0, 10, 6, 12,  },
 {3, 6, 4, 8, 9, 12, 7, 14, 10, 15, 2, 1, 0, 5, 11, 13,  },
 // 13+3
 {13, 12, 1, 0, 7, 15, 4, 6, 14, 5, 8, 3, 11, 9, 2, 10,  },
 {14, 8, 11, 13, 2, 1, 7, 4, 10, 3, 12, 15, 0, 9, 6, 5,  },
 {1, 2, 8, 4, 15, 12, 9, 5, 7, 11, 14, 13, 6, 10, 0, 3,  },
 {15, 9, 12, 10, 5, 3, 0, 6, 2, 11, 1, 7, 8, 14, 4, 13,  },
};

static inline uint8_t Subst(uint8_t idx, uint8_t bits) {
  return PERMS[idx][bits & 15];
}

static inline uint8_t ModularPlus(uint8_t x, uint8_t y) {
  return (x + y) & 15;
}

static inline uint8_t ModularMinus(uint8_t x, uint8_t y) {
  return (x - y) & 15;
}

struct State {
  int64_t num_bits = 0;

  // Packed into eight-bit bytes as
  // (a << 4 | b),(c << 4 | d),...
  // uint8_t halves[8];
  uint64_t data;

  #if 0
  inline uint8_t GetHalf(int i) {
    uint8_t b = halves[i >> 1];
    if (i & 1) {
      return b & 0xF;
    } else {
      return (b >> 4) & 0xF;
    }
  }
  #endif

  inline uint8_t GetHalf(int i) {
    return (data >> (4 * (15 - i))) & 0xF;
    // GetHalf is always called on constants, so it's a bit
    // surprising, but storing the bytes in the other order
    // may give about a 5% speedup (would need to recompute
    // the permutations as well).
    // return (data >> (4 * i)) & 0xF;
  }

  inline void SetHalf(int i, uint8_t b) {
    uint64_t mask = (uint64_t)0b1111 << (4 * (15 - i));
    data &= ~mask;
    data |= (uint64_t)b << (4 * (15 - i));
  }

  State () {
    // for (auto &b : halves) b = 0;
    data = 0;
  }

  inline uint8_t NextBit() {
    num_bits++;
    if ((num_bits % (1 << 29) == 0)) {
      // bigcrush uses close to 2^38 random numbers (32-bit)
      int64_t bits_needed = 32ULL * (1ULL << 38);
      double pct = (num_bits / (double)bits_needed) * 100.0;
      printf("Generated %lld bits (%.3f%%)\n", num_bits, pct);
    }

    if (true) {
      uint8_t o[16];
      repeat<16>([this, &o](size_t i) {
        o[i] = Subst(i, GetHalf(i));
        });

      uint64_t out = 0;
      repeat<16>([&o, &out](size_t i) {
          if ((o[i] & ~0b1111)) __builtin_unreachable();
          out |= ((uint64_t)o[i] << (4 * (15 - i)));
        });

      data = out;
    } else {
      uint64_t out = 0;
      repeat<16>([this, &out](size_t i) {
          out <<= 4;
          out |= Subst(i, GetHalf(i));
        });
      data = out;
    }

    static constexpr std::array<int, 64> bit_indices = {
      49, 44, 34, 41, 0, 29, 40, 50, 39, 59, 8, 52, 35, 38,
      51, 3, 46, 43, 48, 31, 47, 23, 10, 5, 11, 12, 16, 36,
      60, 42, 19, 57, 22, 30, 4, 33, 15, 6, 45, 53, 61, 58,
      24, 54, 26, 63, 17, 55, 37, 56, 28, 2, 9, 1, 27, 62,
      18, 32, 21, 13, 20, 7, 25, 14,
    };

    {
      uint64_t out = 0;
      repeat<64>([this, &out](size_t i) {
        int in_pos = i;
        uint64_t bit = (data >> (63 - in_pos)) & 1;
        int out_pos = bit_indices[i];
        out |= (bit << (63 - out_pos));
        });
      data = out;
    }

    // PERF! Could do two 64-bit masked adds instead.
#if 0
    repeat<16>([this](size_t i) {
        uint8_t b = GetHalf(i);
        b = ModularPlus(CONST[i], b);
        SetHalf(i, b);
      });
#endif
    uint8_t uu = GetHalf(10);
    uint8_t vv = GetHalf(11);
    uint8_t ww = GetHalf(12);
    uint8_t xx = GetHalf(13);
    uint8_t yy = GetHalf(14);
    uint8_t zz = GetHalf(15);

    zz = ModularPlus(zz, xx);
    yy = ModularMinus(yy, xx);
    ww = ModularPlus(ww, uu);
    vv = ModularMinus(vv, uu);

    SetHalf(10, uu);
    SetHalf(11, vv);
    SetHalf(12, ww);
    SetHalf(13, xx);
    SetHalf(14, yy);
    SetHalf(15, zz);

    return GetHalf(0) & 1;
  }

  uint32_t Next() {
    uint32_t ret = 0;
    for (int idx = 0; idx < 32; idx++) {
      ret <<= 1;
      ret |= NextBit();
    }
    return ret;
  }

};


int main(int argc, char **argv) {
  AnsiInit();
  CPrintf("Testing in-process.\n");

  {
    constexpr int NUM_NYBBLES = 16;
    ImageRGBA img(512, NUM_NYBBLES * 4);
    State state;
    for (int x = 0; x < img.Width(); x++) {
      auto Plot = [&img, x](uint8_t bits, int offset) {
          const auto [r, g, b] = ColorUtil::HSVToRGB(
              offset / (float)NUM_NYBBLES, 1.0, 1.0);
          const uint32_t color = ColorUtil::FloatsTo32(r, g, b, 1.0);
          for (int y = 0; y < 4; y++) {
            uint32_t c = (bits & 1) ? color : (color & 0x111111FF);
            img.BlendPixel32(x, offset * 4 + 3 - y, c);
            bits >>= 1;
          }
        };
      (void)state.NextBit();
      for (int i = 0; i < 16; i++) {
        uint8_t h = state.GetHalf(i);
        Plot(h, i);
      }
    }
    img.ScaleBy(4).Save("gen.png");
    CPrintf("Wrote " ABLUE("gen.png") ".\n");
  }

  {
    State state;
    Timer timer;
    constexpr int TIMES = 80000000;
    for (int i = 0; i < TIMES; i++) {
      state.NextBit();
    }
    double sec = timer.Seconds();
    printf("Result %llx in %.4fs (%.1fk/sec)\n",
           state.data, sec, (TIMES / 1024.0) / sec);
  }

  State state;

  char name[] = "in-process";

  unif01_Gen gen;
  gen.state = (void*)&state;
  gen.param = (void*)nullptr;
  gen.name = name;
  gen.GetU01 = +[](void *param, void *state) -> double {
      State *s = (State*)state;
      return (double)s->Next() / (double)0x100000000ULL;
    };

  gen.GetBits = +[](void *param, void *state) -> unsigned long {
      State *s = (State*)state;
      return s->Next();
    };

  gen.Write = +[](void *) {
      printf("(in-process)");
    };

  CPrintf("Running " APURPLE("SmallCrush") "...\n");
  bbattery_SmallCrush(&gen);

  // CPrintf("Running " APURPLE("Crush") "...\n");
  // bbattery_Crush(&gen);

  // CPrintf("Running " APURPLE("BigCrush") "...\n");
  // bbattery_BigCrush(&gen);


  CPrintf("Getting " APURPLE("more stats") "...\n");
  static constexpr int SIZE = 1 << 20;
  static_assert((SIZE % 8) == 0);
  std::vector<uint8_t> vec;
  vec.reserve(SIZE);
  {
    State state;
    state.Next();
    while (vec.size() < SIZE) {
      uint32_t x = state.Next();
      vec.push_back((x >> 24) & 0xFF);
      vec.push_back((x >> 16) & 0xFF);
      vec.push_back((x >> 8) & 0xFF);
      vec.push_back(x & 0xFF);
    }
  }

  {
    int64 bits[2] = {};
    int64 bytes[256] = {};
    for (int i = 0; i < vec.size(); i++) {
      uint8_t byte = vec[i];
      int b = std::popcount<uint8_t>(byte);
      bits[0] += 8 - b;
      bits[1] += b;
      bytes[byte]++;
    }

    printf("%lld zeroes, %lld ones (%.5f%%)\n",
           bits[0], bits[1], (bits[1] * 100.0) / (bits[0] + bits[1]));
    int mini = 0, maxi = 0;
    for (int i = 1; i < 256; i++) {
      if (bytes[i] < bytes[mini]) mini = i;
      if (bytes[i] > bytes[maxi]) maxi = i;
    }

    printf("Rarest byte 0x%02x, %lld times (%.5f%%)\n"
           "Most common 0x%02x, %lld times (%.5f%%)\n"
           "Exactly 1/256 would be (%.5f%%)\n",
           mini, bytes[mini], (bytes[mini] * 100.0) / vec.size(),
           maxi, bytes[maxi], (bytes[maxi] * 100.0) / vec.size(),
           100.0 / 256.0);
  }

  CPrintf(AGREEN("OK") "\n");
  return 0;
}
