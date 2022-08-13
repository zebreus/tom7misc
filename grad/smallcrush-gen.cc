
#include <vector>
#include <cstdint>
#include <bit>
#include <cstring>
#include <string>
#include <functional>

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

#include "testu01.h"

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

static constexpr uint8_t SUBST[256] = {
// Best error was 1952 after 150000 attempts [438.966/sec]
  178, 122, 20, 19, 31, 64, 70, 35, 75, 80, 154, 5, 47, 105, 93, 250, 217, 140, 191, 233, 58, 79, 212, 216, 237, 4, 141, 101, 160, 181, 107, 206, 180, 220, 248, 28, 133, 142, 143, 204, 117, 1, 175, 86, 243, 232, 73, 90, 150, 203, 218, 8, 219, 2, 111, 57, 48, 46, 130, 33, 174, 171, 189, 158, 6, 11, 254, 193, 18, 76, 227, 137, 149, 106, 115, 207, 224, 170, 38, 30, 82, 108, 74, 9, 235, 112, 16, 129, 100, 15, 109, 183, 59, 165, 62, 77, 194, 14, 169, 116, 161, 155, 246, 213, 238, 94, 134, 92, 146, 255, 173, 69, 56, 24, 12, 249, 54, 145, 166, 121, 99, 53, 123, 131, 81, 251, 103, 202, 135, 211, 242, 244, 26, 148, 228, 209, 167, 182, 97, 185, 197, 223, 44, 60, 231, 51, 110, 230, 177, 236, 132, 126, 66, 138, 114, 40, 45, 214, 186, 156, 10, 200, 221, 7, 32, 23, 198, 29, 196, 91, 13, 225, 68, 61, 229, 21, 159, 78, 22, 187, 226, 43, 252, 27, 241, 25, 188, 136, 127, 184, 201, 102, 0, 253, 151, 34, 247, 222, 49, 71, 50, 36, 118, 195, 128, 72, 120, 190, 240, 52, 84, 208, 157, 104, 88, 205, 42, 124, 89, 95, 163, 144, 147, 113, 41, 83, 39, 199, 172, 96, 152, 17, 153, 125, 55, 192, 67, 3, 119, 63, 234, 87, 176, 245, 164, 85, 65, 215, 179, 168, 98, 139, 162, 210, 239, 37,
};

static inline uint8_t ModularPlus(uint8_t x, uint8_t y) {
  return (x + y) & 0xFF;
}

static inline uint8_t ModularMinus(uint8_t x, uint8_t y) {
  return (x - y) & 0xFF;
}

static inline uint64_t Permute(unit64_t data) {
  // Reading bits from left (msb) to right (lsb), this gives
  // the output location for each bit. So for example the
  // first entry says that the 0th bit in the input is sent
  // to the 49th bit in the output.
  static constexpr std::array<int, 64> bit_indices = {
    49, 44, 34, 41, 0, 29, 40, 50, 39, 59, 8, 52, 35, 38,
    51, 3, 46, 43, 48, 31, 47, 23, 10, 5, 11, 12, 16, 36,
    60, 42, 19, 57, 22, 30, 4, 33, 15, 6, 45, 53, 61, 58,
    24, 54, 26, 63, 17, 55, 37, 56, 28, 2, 9, 1, 27, 62,
    18, 32, 21, 13, 20, 7, 25, 14,
  };

  uint64_t out = 0;
  repeat<64>([this, &out](size_t i) {
      int in_pos = i;
      uint64_t bit = (data >> (63 - in_pos)) & 1;
      int out_pos = bit_indices[i];
      out |= (bit << (63 - out_pos));
    });
  return out;
}

struct State {
  int64_t num_bits = 0;

  // Packed into eight-bit bytes as
  // a << 56 | b << 48 | ...
  uint64_t data;

  inline uint8_t GetByte(int i) {
    return (data >> (8 * (7 - i))) & 0xFF;
    // GetHalf is always called on constants, so it's a bit
    // surprising, but storing the bytes in the other order
    // may give about a 5% speedup (would need to recompute
    // the permutations as well).
    // return (data >> (4 * i)) & 0xF;
  }

  inline void SetByte(int i, uint8_t b) {
    uint64_t mask = (uint64_t)0b11111111 << (8 * (7 - i));
    data &= ~mask;
    data |= (uint64_t)b << (8 * (7 - i));
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

    uint64_t out = 0;
    repeat<8>([this, &out](size_t i) {
        out <<= 8;
        out |= SUBST[GetByte(i)];
      });
    data = out;

    data = Permute(data);

    uint8_t aa = GetByte(0);
    uint8_t bb = GetByte(1);
    uint8_t cc = GetByte(2);
    uint8_t dd = GetByte(3);
    uint8_t ee = GetByte(4);
    uint8_t ff = GetByte(5);
    uint8_t gg = GetByte(6);
    uint8_t hh = GetByte(7);

    aa = ModularPlus(aa, bb);
    cc = ModularMinus(cc, bb);

    dd = ModularPlus(dd, ee);
    gg = ModularMinus(gg, ee);

    SetByte(0, aa);
    SetByte(1, bb);
    SetByte(2, cc);
    SetByte(3, dd);
    SetByte(4, ee);
    SetByte(5, ff);
    SetByte(6, gg);
    SetByte(7, hh);

    return GetByte(0) & 1;
  }

  uint32_t Next() {
    uint32_t ret = 0;

    for (int idx = 0; idx < 32; idx++) {
      ret <<= 1;
      ret |= NextBit();
    }

    /*
    for (int idx = 0; idx < 9; idx++)
      (void)NextBit();
    ret = data & 0xFFFFFFFF;
    */
    return ret;
  }

};

namespace {
struct TheGenerator : public Generator {
  State state;
  char name[16];
  TheGenerator() {
    strcpy(name, "in-process");
  }

  void FillGen(unif01_Gen *gen) override {

    gen->state = (void*)&state;
    gen->param = (void*)nullptr;
    gen->name = name;
    gen->GetU01 = +[](void *param, void *state) -> double {
        State *s = (State*)state;
        return (double)s->Next() / (double)0x100000000ULL;
    };

    gen->GetBits = +[](void *param, void *state) -> unsigned long {
        State *s = (State*)state;
        return s->Next();
      };

    gen->Write = +[](void *) {
        printf("(in-process)");
      };
  }
};
}  // namespace

int main(int argc, char **argv) {
  AnsiInit();
  CPrintf("Testing in-process.\n");

  {
    constexpr int NUM_BYTES = 8;
    ImageRGBA img(512, NUM_BYTES * 8);
    State state;
    for (int x = 0; x < img.Width(); x++) {
      auto Plot = [&img, x](uint8_t bits, int offset) {
          const auto [r, g, b] = ColorUtil::HSVToRGB(
              offset / (float)NUM_BYTES, 1.0, 1.0);
          const uint32_t color = ColorUtil::FloatsTo32(r, g, b, 1.0);
          for (int y = 0; y < 8; y++) {
            uint32_t c = (bits & 1) ? color : (color & 0x111111FF);
            img.BlendPixel32(x, offset * 8 + 7 - y, c);
            bits >>= 1;
          }
        };
      (void)state.NextBit();
      for (int i = 0; i < 8; i++) {
        uint8_t h = state.GetByte(i);
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

  ParallelBigCrush([]() { return new TheGenerator; },
                   "gen");

  // CPrintf("Running " APURPLE("SmallCrush") "...\n");
  // bbattery_SmallCrush(&gen);

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
