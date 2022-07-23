
#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <immintrin.h>

#include "base/logging.h"
#include "base/stringprintf.h"
#include "timer.h"

using namespace std;

template<std::size_t N, class F, std::size_t START = 0>
inline void repeat(const F &f) {
  if constexpr (N == 0) {
    return;
  } else {
    f(START);
    repeat<N - 1, F, START + 1>(f);
  }
}


template<size_t N>
static inline constexpr uint64_t BitsToMask(const std::array<uint8_t, N> &readfrom) {
  uint64_t mask = 0;
  for (int i = 0; i < N; i++) {
    mask |= (uint64_t{1} << (63 - readfrom[i]));
  }
  return mask;
}

// read N bit positions from in (which must be in strictly ASCENDING ORDER),
// where 0 is the MSB, and write them to the low order bits of out, after
// shifting it up N.
template<uint8_t... U8s>
static inline void ExtractInOrder(uint64_t in,
                                  uint64_t &out) {
  static_assert(sizeof... (U8s) > 0);
  if constexpr (sizeof... (U8s) == 1) {
    // Don't bother with a single bit.
      // (Is there a better way to just get the single element
      // from the parameter pack?)
      constexpr uint8_t pos[1] = {U8s...};
    uint64_t bit = (in >> (63 - ( pos[0] ))) & 1;
    out <<= 1;
    out |= bit;

  } else {
    constexpr array<uint8_t, sizeof... (U8s)> ra { U8s... };
    constexpr uint64_t mask = BitsToMask(ra);
    const uint64_t bits = _pext_u64(in, mask);
    uint64_t res = (out << ra.size()) | bits;
    /*
      printf("In %llx, Out %llx, Mask %llx, Bits %llx, Res %llx\n",
      in, out, mask, bits, res);
    */
    // out <<= ra.size();
    // out |= bits;
    out = res;
  }
}

static constexpr std::array<uint8_t, 64> bit_indices = {
  49, 44, 34, 41, 0, 29, 40, 50, 39, 59, 8, 52, 35, 38,
  51, 3, 46, 43, 48, 31, 47, 23, 10, 5, 11, 12, 16, 36,
  60, 42, 19, 57, 22, 30, 4, 33, 15, 6, 45, 53, 61, 58,
  24, 54, 26, 63, 17, 55, 37, 56, 28, 2, 9, 1, 27, 62,
  18, 32, 21, 13, 20, 7, 25, 14,
};

static constexpr std::array<uint8_t, 64> inverted = {
  4, 53, 51, 15, 34, 23, 37, 61, 10, 52, 22, 24, 25, 59,
  63, 36, 26, 46, 56, 30, 60, 58, 32, 21, 42, 62, 44, 54,
  50, 5, 33, 19, 57, 35, 2, 12, 27, 48, 13, 8, 6, 3, 29,
  17, 1, 38, 16, 20, 18, 0, 7, 14, 11, 39, 43, 47, 49,
  31, 41, 9, 28, 40, 55, 45,
};

static void Invert() {
  std::array<uint8_t, 64> inv;
  for (int i = 0; i < 64; i++) {
    inv[bit_indices[i]] = i;
  }
  printf("Inverted:\n{");
  for (int x : inv) printf(" %d,", x);
  printf("}\n");
}

static inline uint64_t Method1(uint64_t in) {
  uint64_t out = 0;
  for (int i = 0; i < 64; i++) {
    int in_pos = i;
    uint64_t bit = (in >> (63 - in_pos)) & 1;
    int out_pos = bit_indices[i];
    out |= (bit << (63 - out_pos));
  }
  return out;
}

// Unrolled, about twice as fast!
static inline uint64_t Method2(uint64_t in) {
  uint64_t out = 0;
  repeat<64>([in, &out](std::size_t i) {
      int in_pos = i;
      uint64_t bit = (in >> (63 - in_pos)) & 1;
      int out_pos = bit_indices[i];
      out |= (bit << (63 - out_pos));
    });
  return out;
}

static inline uint64_t Method3(uint64_t in) {
  uint64_t out = 0;

  ExtractInOrder<4>(in, out);
  ExtractInOrder<53>(in, out);
  ExtractInOrder<51>(in, out);
  ExtractInOrder<15>(in, out);
  ExtractInOrder<34>(in, out);

  ExtractInOrder<23>(in, out);
  ExtractInOrder<37>(in, out);
  ExtractInOrder<61>(in, out);
  ExtractInOrder<10>(in, out);
  ExtractInOrder<52>(in, out);
  ExtractInOrder<22>(in, out);
  ExtractInOrder<24>(in, out);
  ExtractInOrder<25>(in, out);
  ExtractInOrder<59>(in, out);
  ExtractInOrder<63>(in, out);
  ExtractInOrder<36>(in, out);
  ExtractInOrder<26>(in, out);
  ExtractInOrder<46>(in, out);
  ExtractInOrder<56>(in, out);
  ExtractInOrder<30>(in, out);
  ExtractInOrder<60>(in, out);
  ExtractInOrder<58>(in, out);
  ExtractInOrder<32>(in, out);
  ExtractInOrder<21>(in, out);
  ExtractInOrder<42>(in, out);
  ExtractInOrder<62>(in, out);
  ExtractInOrder<44>(in, out);
  ExtractInOrder<54>(in, out);
  ExtractInOrder<50>(in, out);
  ExtractInOrder<5>(in, out);
  ExtractInOrder<33>(in, out);
  ExtractInOrder<19>(in, out);
  ExtractInOrder<57>(in, out);
  ExtractInOrder<35>(in, out);
  ExtractInOrder<2>(in, out);
  ExtractInOrder<12>(in, out);
  ExtractInOrder<27>(in, out);
  ExtractInOrder<48>(in, out);
  ExtractInOrder<13>(in, out);
  ExtractInOrder<8>(in, out);
  ExtractInOrder<6>(in, out);
  ExtractInOrder<3>(in, out);
  ExtractInOrder<29>(in, out);
  ExtractInOrder<17>(in, out);
  ExtractInOrder<1>(in, out);
  ExtractInOrder<38>(in, out);
  ExtractInOrder<16>(in, out);
  ExtractInOrder<20>(in, out);
  ExtractInOrder<18>(in, out);
  ExtractInOrder<0>(in, out);
  ExtractInOrder<7>(in, out);
  ExtractInOrder<14>(in, out);
  ExtractInOrder<11>(in, out);
  ExtractInOrder<39>(in, out);
  ExtractInOrder<43>(in, out);
  ExtractInOrder<47>(in, out);
  ExtractInOrder<49>(in, out);
  ExtractInOrder<31>(in, out);
  ExtractInOrder<41>(in, out);
  ExtractInOrder<9>(in, out);
  ExtractInOrder<28>(in, out);
  ExtractInOrder<40>(in, out);
  ExtractInOrder<55>(in, out);
  ExtractInOrder<45>(in, out);

  return out;
}

static inline uint64_t Method4(uint64_t in) {
  uint64_t out = 0;

  ExtractInOrder<4, 53>(in, out);
  ExtractInOrder<51>(in, out);
  ExtractInOrder<15, 34>(in, out);

  ExtractInOrder<23, 37, 61>(in, out);
  ExtractInOrder<10, 52>(in, out);
  ExtractInOrder<22, 24, 25, 59, 63>(in, out);
  ExtractInOrder<36>(in, out);
  ExtractInOrder<26, 46, 56>(in, out);
  ExtractInOrder<30, 60>(in, out);
  ExtractInOrder<58>(in, out);
  ExtractInOrder<32>(in, out);
  ExtractInOrder<21, 42, 62>(in, out);
  ExtractInOrder<44, 54>(in, out);
  ExtractInOrder<50>(in, out);
  ExtractInOrder<5, 33>(in, out);
  ExtractInOrder<19, 57>(in, out);
  ExtractInOrder<35>(in, out);
  ExtractInOrder<2, 12, 27, 48>(in, out);
  ExtractInOrder<13>(in, out);
  ExtractInOrder<8>(in, out);
  ExtractInOrder<6>(in, out);
  ExtractInOrder<3, 29>(in, out);
  ExtractInOrder<17>(in, out);
  ExtractInOrder<1, 38>(in, out);
  ExtractInOrder<16, 20>(in, out);
  ExtractInOrder<18>(in, out);
  ExtractInOrder<0, 7, 14>(in, out);
  ExtractInOrder<11, 39, 43, 47, 49>(in, out);
  ExtractInOrder<31, 41>(in, out);
  ExtractInOrder<9, 28, 40, 55>(in, out);
  ExtractInOrder<45>(in, out);

  return out;
}


template<class F>
double RunOne(const std::string &name, F f) {
  constexpr int TIMES = 2000000;

  Timer timer;
  uint64_t state = 0xCAFEBEEF12345678;
  // uint64_t state = 0xAAAAAAAABEEEEEEF;
  for (int i = 0; i < TIMES; i++)
    state = f(state);
  double sec = timer.Seconds();
  // CHECK(state == 0x91b7d6ecb75846bd) << StringPrintf("%llx", state);
  printf("%s: %llx\n", name.c_str(), state);
  printf("%s: %.4f sec; %.3f/sec\n", name.c_str(), sec, TIMES / sec);
  return sec;
}

int main (int argc, char **argv) {
  // Invert();

  RunOne("method1", Method1);
  RunOne("method2", Method2);
  RunOne("method3", Method3);
  RunOne("method4", Method4);

  return 0;
}
