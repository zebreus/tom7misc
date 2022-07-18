
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

static constexpr int TARGET_FLOATS = 51320000;

using int64 = int64_t;

static const uint8_t PERMS[16][16] = {
  // length 5 cycle, length 11 cycle; non-overlapping.
 {4, 3, 10, 0, 8, 13, 5, 11, 1, 6, 14, 15, 2, 7, 9, 12,  },
 {1, 15, 4, 10, 6, 13, 5, 14, 11, 3, 7, 0, 9, 12, 2, 8,  },
 {3, 15, 7, 2, 8, 12, 0, 6, 1, 14, 5, 9, 13, 11, 4, 10,  },
 {15, 5, 13, 1, 11, 12, 14, 8, 10, 0, 6, 7, 4, 9, 3, 2,  },
 {7, 9, 13, 2, 6, 3, 12, 4, 14, 5, 1, 8, 0, 11, 15, 10,  },
 {14, 10, 13, 6, 3, 9, 12, 2, 4, 0, 7, 5, 1, 15, 11, 8,  },
 {15, 3, 6, 9, 11, 4, 12, 1, 7, 8, 5, 14, 0, 2, 13, 10,  },
 {11, 9, 14, 6, 3, 15, 8, 1, 5, 13, 7, 12, 4, 10, 0, 2,  },
 /*
 {11, 6, 8, 1, 3, 2, 10, 9, 7, 5, 14, 13, 0, 4, 15, 12,  },
 {8, 6, 10, 14, 5, 0, 7, 12, 1, 4, 13, 15, 3, 11, 9, 2,  },
 {9, 12, 5, 7, 6, 0, 13, 4, 10, 11, 1, 14, 2, 3, 15, 8,  },
 {2, 0, 6, 13, 11, 8, 5, 12, 10, 1, 14, 3, 9, 15, 7, 4,  },
 {1, 9, 11, 6, 14, 4, 12, 3, 2, 15, 5, 10, 13, 7, 0, 8,  },
 {12, 15, 1, 0, 5, 6, 14, 13, 4, 10, 7, 3, 2, 11, 8, 9,  },
 {10, 12, 1, 7, 5, 11, 0, 6, 14, 4, 15, 13, 3, 9, 2, 8,  },
 {1, 2, 15, 0, 11, 10, 13, 3, 9, 12, 14, 7, 4, 5, 6, 8,  },
 */
 // maximal cycles
  {11, 0, 4, 9, 14, 12, 2, 1, 7, 8, 5, 15, 13, 6, 3, 10},
  {14, 0, 6, 9, 12, 13, 5, 2, 10, 1, 4, 3, 15, 8, 7, 11},
  {9, 8, 7, 4, 5, 13, 3, 11, 10, 6, 2, 12, 14, 15, 0, 1},
 /*
  {1, 10, 8, 9, 5, 3, 13, 6, 4, 14, 7, 0, 2, 15, 11, 12},
  {11, 4, 15, 10, 8, 1, 0, 9, 12, 5, 14, 3, 6, 7, 2, 13},
  {11, 10, 15, 5, 0, 12, 4, 13, 14, 1, 3, 9, 2, 6, 7, 8},
  {6, 10, 3, 4, 15, 1, 8, 0, 9, 13, 2, 5, 14, 11, 7, 12},
  {3, 4, 15, 12, 9, 13, 5, 11, 2, 0, 6, 10, 8, 1, 7, 14},
  {5, 6, 3, 12, 11, 2, 9, 13, 7, 15, 0, 8, 14, 1, 4, 10},
  {3, 2, 9, 10, 14, 7, 4, 15, 12, 5, 13, 1, 6, 8, 11, 0},
  {3, 15, 12, 1, 11, 6, 9, 0, 14, 2, 5, 7, 13, 8, 4, 10},
  {3, 10, 12, 8, 13, 1, 9, 11, 6, 4, 2, 14, 7, 5, 15, 0},
  {2, 3, 10, 14, 8, 0, 5, 4, 1, 7, 13, 9, 11, 15, 6, 12},
  {5, 11, 1, 13, 3, 15, 0, 6, 9, 12, 4, 10, 14, 8, 7, 2},
  {15, 13, 10, 11, 12, 4, 0, 3, 1, 14, 8, 6, 2, 7, 5, 9},
  {13, 12, 7, 1, 2, 4, 3, 0, 5, 11, 6, 10, 15, 9, 8, 14},
 */

 // 13+3
 {4, 15, 6, 0, 3, 13, 12, 1, 7, 11, 14, 2, 5, 10, 8, 9,  },
 {3, 13, 10, 7, 11, 12, 8, 0, 2, 15, 9, 5, 6, 4, 1, 14,  },
 {12, 13, 9, 1, 10, 3, 2, 0, 5, 15, 8, 14, 7, 6, 4, 11,  },
 {13, 4, 11, 14, 2, 6, 7, 1, 3, 0, 5, 15, 8, 9, 10, 12,  },
 {5, 8, 15, 1, 12, 7, 4, 2, 3, 10, 6, 0, 11, 9, 13, 14,  },
 /*
 {4, 7, 15, 2, 6, 8, 11, 9, 10, 1, 14, 12, 13, 5, 3, 0,  },
 {14, 11, 3, 6, 7, 13, 9, 5, 0, 12, 4, 15, 8, 2, 10, 1,  },
 {3, 5, 15, 12, 0, 11, 4, 14, 6, 1, 2, 13, 9, 7, 8, 10,  },
 {1, 12, 9, 15, 3, 14, 7, 0, 13, 5, 8, 2, 10, 11, 6, 4,  },
 {14, 5, 8, 12, 1, 2, 11, 3, 6, 13, 9, 0, 7, 4, 15, 10,  },
 {12, 9, 0, 14, 13, 4, 10, 3, 2, 6, 15, 1, 7, 5, 11, 8,  },
 {10, 3, 12, 9, 14, 4, 15, 2, 7, 13, 1, 0, 11, 6, 5, 8,  },
 {12, 14, 11, 8, 13, 2, 4, 0, 7, 3, 15, 1, 10, 6, 9, 5,  },
 {4, 10, 6, 12, 14, 9, 8, 1, 7, 11, 0, 5, 13, 15, 3, 2,  },
 {2, 15, 11, 6, 8, 14, 12, 10, 0, 7, 4, 3, 1, 5, 13, 9,  },
 {10, 12, 6, 11, 5, 3, 13, 8, 9, 7, 15, 2, 14, 1, 0, 4,  },
 */
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
  int64 num_bits = 0;

  // each half represented four bits
  uint32_t a : 4, b : 4, c : 4, d : 4,
           e : 4, f : 4, g : 4, h : 4;
  uint32_t i : 4, j : 4, k : 4, l : 4,
           m : 4, n : 4, o : 4, p : 4;

  State () {
    a = 1; b = 2; c = 3; d = 4;
    e = 14; f = 13; g = 12; h = 11;
    i = 5; j = 6; k = 7; l = 8;
    m = 9; n = 10; o = 10; p = 15;
  }

  inline uint8_t NextBit() {
    num_bits++;
    if ((num_bits % (1 << 29) == 0)) {
      printf("Generated %lld bits\n", num_bits);
    }

    uint8_t aa = Subst(0, b);
    uint8_t bb = Subst(1, c);
    uint8_t cc = Subst(2, d);
    uint8_t dd = Subst(8, a);

    uint8_t ee = Subst(4, f);
    uint8_t ff = Subst(5, g);
    uint8_t gg = Subst(6, h);
    uint8_t hh = Subst(9, e);

    uint8_t ii = Subst(7, j);
    uint8_t jj = Subst(10, k);
    uint8_t kk = Subst(11, l);
    uint8_t ll = Subst(12, m);

    uint8_t mm = Subst(13, n);
    uint8_t nn = Subst(14, o);
    uint8_t oo = Subst(15, p);
    uint8_t pp = Subst(3, i);


    aa = ModularPlus(aa, ii);
    bb = ModularPlus(bb, ff);
    cc = ModularPlus(cc, oo);
    dd = ModularPlus(dd, hh);

    hh = ModularPlus(hh, aa);
    ii = ModularPlus(ii, ee);


    a = aa;
    b = bb;
    c = cc;
    d = dd;
    e = ee;
    f = ff;
    g = gg;
    h = hh;
    i = ii;
    j = jj;
    k = kk;
    l = ll;
    m = mm;
    n = nn;
    o = oo;
    p = pp;

    return a & 1;
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
      Plot(state.a, 0);
      Plot(state.b, 1);
      Plot(state.c, 2);
      Plot(state.d, 3);
      Plot(state.e, 4);
      Plot(state.f, 5);
      Plot(state.g, 6);
      Plot(state.h, 7);
      Plot(state.i, 8);
      Plot(state.j, 9);
      Plot(state.k, 10);
      Plot(state.l, 11);
      Plot(state.m, 12);
      Plot(state.n, 13);
      Plot(state.o, 14);
      Plot(state.p, 15);
    }
    img.ScaleBy(4).Save("gen.png");
    CPrintf("Wrote " ABLUE("gen.png") ".\n");
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

  // CPrintf("Running " APURPLE("SmallCrush") "...\n");
  // bbattery_SmallCrush(&gen);

  CPrintf("Running " APURPLE("Crush") "...\n");
  bbattery_Crush(&gen);

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
