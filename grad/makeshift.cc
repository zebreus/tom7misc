

#include <cstdint>
#include <cstdio>
#include <bit>
#include <unordered_set>

#include "half.h"
#include "opt/optimizer.h"
#include "timer.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "ansi.h"

using namespace half_float;
using namespace half_float::literal;

using namespace std;

static constexpr inline uint16_t GetU16(half_float::half h) {
  return std::bit_cast<uint16_t, half_float::half>(h);
}

static constexpr inline half_float::half GetHalf(uint16_t u) {
  return std::bit_cast<half_float::half, uint16_t>(u);
}

template<class F>
static double ForAll(F f) {
  double dist = 0.0;
  for (int x = 0; x < 256; x++) {
    dist += f((uint8_t)x, (half)x);
  }
  return dist;
}

// Same, for 9-bit expressions
template<class F>
static double ForAll9(F f) {
  double dist = 0.0;
  for (int x = 0; x < 512; x++) {
    dist += f((uint16_t)x, (half)x);
  }
  return dist;
}

[[maybe_unused]]
static double TestRightShift4(double fudge, double offset) {
  return
    ForAll(
        [fudge, offset](uint8_t x, half xh) {
          uint8_t z = x >> 4;

          half zh =
            ((xh * (1.0_h / 16.0_h) - (half)fudge) +
             (half)offset - (half)offset);

          double dist = zh - (half)z;
          return dist * dist;
        });
}

[[maybe_unused]]
static double TestRightShift1(double a, double b) {
  return
    ForAll(
        [a, b](uint8_t x, half xh) {
          uint8_t z = x >> 1;
          half zh = xh * (half)a + (half)b - (half)b;
          double dist = zh - (half)z;
          return dist * dist;
        });
}

static double TestRightShift2(double a, double b, double c) {
  return
    ForAll(
        [a, b, c](uint8_t x, half xh) {
          uint8_t z = x >> 2;
          // Didn't find any solutions for e.g. x * a + b - c.
          half zh = xh * 0.25_h + (half)a + (half)b - (half)c;
          double dist = zh - (half)z;
          return dist * dist;
        });
}

// (Didn't actually try smaller expressions here.)
static double TestRightShift3(double a, double b, double c) {
  return
    ForAll(
        [a, b, c](uint8_t x, half xh) {
          uint8_t z = x >> 3;
          half zh = xh * 0.125_h + (half)a + (half)b - (half)c;
          double dist = zh - (half)z;
          return dist * dist;
        });
}

static double TestRightShift7(double a, double b, double c) {
  return
    ForAll(
        [a, b, c](uint8_t x, half xh) {
          uint8_t z = x >> 7;
          // half zh = xh * (1.0_h / 256.0_h) + (half)a + (half)b - (half)c;
          half zh = xh * (half)a + (half)b - (half)c;
          double dist = zh - (half)z;
          return dist * dist;
        });
}

static double TestRightShift7Two(double a, double b) {
  return
    ForAll(
        [a, b](uint8_t x, half xh) {
          uint8_t z = x >> 7;
          half zh = (xh + (half)a) * (half)b;
          double dist = zh - (half)z;
          return dist * dist;
        });
}


// Note: Tests in 0..511
static double TestRightShift8(double a, double b, double c) {
  return
    ForAll9(
        [a, b, c](uint16_t x, half xh) {
          uint16_t z = x >> 8;
          half zh = xh * (1.0_h / 256.0_h) + (half)a + (half)b - (half)c;
          double dist = zh - (half)z;
          return dist * dist;
        });
}


using ShiftOpt = Optimizer<0, 2, char>;

[[maybe_unused]]
static void OptimizeShift() {
  ShiftOpt opt([](const ShiftOpt::arg_type &arg) {
      const auto [a, b] = arg.second;
      double err = TestRightShift7Two(a, b);
      return std::make_pair(err, std::make_optional('x'));
    }, time(nullptr));

  opt.Run({},
          {
            // make_pair(-0.51, 0.51),
            make_pair(1.0 / 300.0, 32800.0),
            make_pair(0.0, 15049.0),
            // make_pair(255.0, 2049.0),
          },
          nullopt, nullopt, {60.0}, {0.0});

  const auto [arg, score, out_] = opt.GetBest().value();
  const auto &[a, b] = arg.second;
  printf("a: %.19g = %.11g (0x%04x)\n"
         "b: %.19g = %.11g (0x%04x)\n"
         // "c: %.19g = %.11g (0x%04x)\n"
         "score %.19g\n",
         a, (float)(half)a, GetU16((half)a),
         b, (float)(half)b, GetU16((half)b),
         // c, (float)(half)c, GetU16((half)c),
         score);
}

// Make a shift-add network to compute IsntZero.
// We want some function that is of the form
//
// a0 = a
// an = (ai >> b) + (aj >> c)    (with i,j < n, obviously)
// ...
// return am;
static inline bool Works(/* int i1, int j1, */ int b1, int c1,
                         int i2, int j2, int b2, int c2,
                         int i3, int j3, int b3, int c3,
                         int i4, int j4, int b4, int c4,
                         int i5, int j5, int b5, int c5
                         ) {
  static constexpr int i1 = 0, j1 = 0;
  int a[6];
  // No need to test zero, as any such network returns 0 for 0.
  for (int i = 1; i < 256; i++) {
    a[0] = i;
    a[1] = (a[i1] >> b1) + (a[j1] >> c1);
    // Don't allow overflow.
    if (a[1] & 256) return false;

    a[2] = (a[i2] >> b2) + (a[j2] >> c2);
    // Don't allow overflow.
    if (a[2] & 256) return false;

    a[3] = (a[i3] >> b3) + (a[j3] >> c3);
    // Don't allow overflow.
    if (a[3] & 256) return false;

    a[4] = (a[i4] >> b4) + (a[j4] >> c4);
    // Don't allow overflow.
    if (a[4] & 256) return false;

    a[5] = (a[i5] >> b5) + (a[j5] >> c5);
    // Don't allow overflow.
    if (a[5] & 256) return false;


    if (a[5] != 0x01) return false;
  }
  return true;
}

// Nothing for depth 3, 4, 5. 6 takes hours+

[[maybe_unused]]
static void MakeShiftAdd() {
  Timer timer;

  for (int b1 = 0; b1 < 8; b1++)
  for (int c1 = b1 + 1; c1 < 8; c1++)
  for (int i2 = 0; i2 < 2; i2++)
  for (int j2 = 0; j2 < 2; j2++)
  for (int b2 = 0; b2 < 8; b2++)
  for (int c2 = 0; c2 < 8; c2++)
  for (int i3 = 0; i3 < 3; i3++)
  for (int j3 = 0; j3 < 3; j3++)
  for (int b3 = 0; b3 < 8; b3++)
  for (int c3 = 0; c3 < 8; c3++)
  for (int i4 = 0; i4 < 4; i4++)
  for (int j4 = 0; j4 < 4; j4++)
  for (int b4 = 0; b4 < 8; b4++)
  for (int c4 = 0; c4 < 8; c4++)
  for (int i5 = 0; i5 < 5; i5++)
  for (int j5 = 0; j5 < 5; j5++)
  for (int b5 = 0; b5 < 8; b5++)
  for (int c5 = 0; c5 < 8; c5++) {


    if (Works(b1, c1,
              i2, j2, b2, c2,
              i3, j3, b3, c3,
              i4, j4, b4, c4,
              i5, j5, b5, c5
              )) {
      printf("Success!\n"
             "0 0 %d %d\n"
             "%d %d %d %d\n"
             "%d %d %d %d\n"
             "%d %d %d %d\n"
             "%d %d %d %d\n",
             b1, c1,
             i2, j2, b2, c2,
             i3, j3, b3, c3,
             i4, j4, b4, c4,
             i5, j5, b5, c5
             );
      return;
    }

  }

  printf("No solutions for this depth (%.3f sec)\n",
         timer.Seconds());
}


// Not bad: in mask 0xFF, output 0b00111111
    /*
  .!Best score: 1599
Params:
  2089.41114862224822 = 2090 (0x6815)
  0.2500382336406887229 = 0.25 (0x3400)
  1117.052210467358464 = 1117 (0x645d)
    */

static inline half WorkingF3(half xh) {
  static constexpr double a = 2089.41114862224822;
  static constexpr double b = 0.2500382336406887229;
  static constexpr double c = 1117.052210467358464;
  return (xh + (half)a - (half)a) * (half)b + (half)c - (half)c;
}

// Better. Outputs 0xFF -> 0b00001111
static inline half WorkingF2a(half xh) {
  static constexpr half a = GetHalf(0x4b80);  // 15
  static constexpr half b = GetHalf(0x7bf7);  // 65248
  static constexpr half c = GetHalf(0x2800);  // 0.03125
  return (xh + a + b - b) * c;
}

// Then 0x0F -> 0x01
// Didn't spend much time looking for smaller versions of this, though!
static inline half WorkingF2b(half xh) {
  static constexpr half a = GetHalf(0x477a);  // 7.4765...
  static constexpr half b = GetHalf(0x741f);  // 16880
  static constexpr half c = GetHalf(0x2c00);  // 0.0625
  return (xh + a + b - b) * c;
}

static inline half F3old(double a, double b, double c, half xh) {
  return (xh + (half)a - (half)a) * (half)b + (half)c - (half)c;
}

static inline half F3(double a, double b, double c, half xh) {
  return (xh + (half)a + (half)b - (half)b) * (half)c;
}

static inline half F2(double a, double b, half xh) {
  return (xh + (half)15 + (half)a - (half)a) * (half)b;
}



// Oy! actually it doesn't work
static inline half F3b(double a, double b, double c, half xh) {
  return (xh + (half)65248.0 - (half)65248.0) * (0.03125_h);
  // return (xh + (half)64800.0 - (half)64800.0) * (0.03125_h);
  // return (xh + (half)65280.0 - (half)65280.0) * (0.03125_h);
}


// Another approach to IsntZero that works directly on halfs.
// The idea here is to find functions that preserve the
// nonzeroness, but that reduce the set of bits that are
// ever set.
template<uint8_t IN_MASK>
static double Compress(const std::array<double, 3> &arg) {
  // Compute the function.
  auto F = [&arg](uint8_t x) {
      const auto &[a, b, c] = arg;
      half xh = (half)x;
      return F3(a, b, c, xh);
    };


  // We need to distinguish zero from nonzero, so zero has
  // to output zero!
  double zero_penalty = 0.0;
  // How far are we from every output being integral? This
  // is essential.
  double integer_dist = 0.0;
  // Is anything out of gamut? If so, this is especially bad.
  double bounds_penalty = 0.0;

  // Check zero.
  {
    half zh = F(0);
    if (zh != 0.0_h) {
      zero_penalty += (float)zh * (float)zh;
    }
  }

  // And then given valid functions, we want to minimize the
  // size of this.
  uint8_t bitset = 0;

  // Nonzero arguments.
  for (int x = 1; x < 256; x++) {
    if (x == (x & IN_MASK)) {
      half zh = F(x);
      float zf = zh;
      if (zf < 0.0f) bounds_penalty += zf * zf;
      else if (zf > 255.0f) bounds_penalty += (zf - 255.0f) * (zf - 255.0f);
      else {
        int zi = roundf(zf);
        float di = zh - (half)zi;

        if (zi == 0) {
          // the input is nonzero, so the output must be nonzero
          zero_penalty += 1.0;
        }

        if (di == 0.0f) {
          // If integral, add to bit set.
          bitset |= zi;
        } else {
          // we want some gradient towards integers, but getting
          // really close is not good enough! Floor this so that
          // the penalty is always significant.
          float dmin = std::max(0.00125f, di * di);
          integer_dist += dmin;
        }
      }
    }
  }

  return
    // Gradient towards valid solutions
    bounds_penalty * 1000000000.0 +
    zero_penalty *    100000000.0 +
    integer_dist *      1000000.0 +
    // This is the main thing we're optimizing for
    std::popcount(bitset) * 256 +
    // But all else equal, we'd prefer the bits to be low-order ones.
    bitset;
}


template<uint8_t IN_MASK>
static void MakeCompress() {
#if 1
  using CompressOpt = Optimizer<0, 3, char>;

  CompressOpt opt([](const CompressOpt::arg_type &arg) {
      double err = Compress<IN_MASK>(arg.second);
      return std::make_pair(err, std::make_optional('x'));
    }, time(nullptr));

  opt.Run({},
          {
            make_pair(-1.0, 32768.0),
            make_pair(-1.0, 65248.0),
            make_pair(1 / 256.0, 1 / 2.0),
          },
          nullopt, nullopt, {60.0}, {0.0});

  const auto [arg, score, out_] = opt.GetBest().value();
  printf("Best score: %.19g\nParams:\n", score);
  for (int i = 0; i < arg.second.size(); i++) {
    double a = arg.second[i];
    printf("  %.19g = %.11g (0x%04x)\n",
           a, (float)(half)a, GetU16((half)a));
  }
#else

  // Test manually
  const std::pair<char, std::array<double, 2>> arg =
    {'x',
     {
       65248.0,
       0.03125,
     }
    };

  double score = Compress<IN_MASK>(arg.second);
  printf("Score: %.6f\n", score);
#endif

  auto F = [&arg](uint8_t x) {
      const auto &[a, b, c] = arg.second;
      half xh = (half)x;
      return F3(a, b, c, xh);
    };

  auto StringBits = [](uint8_t zi) {
      string bits(8, '?');
      for (int b = 0; b < 8; b++) {
        bits[b] = (zi & (1 << (7 - b))) ? '1' : '0';
      }
      return bits;
    };

  uint8_t mask = 0;
  bool ok = true;
  for (int i = 0; i < 256; i++) {
    if (i == (i & IN_MASK)) {
      half zh = F(i);
      int zi = zh;
      printf("%02x: ", i);
      if (zh != (half)zi) {
        printf(ARED("%.11g") "\n", (float)zh);
        ok = false;
      } else if (zi < 0 || zi >= 256) {
        printf(ARED("%d") "\n", zi);
        ok = false;
      } else if ((zi == 0) != (i == 0)) {
        printf("%02x: " ARED("%02x") " (%s)\n", i, zi, StringBits(zi).c_str());
        ok = false;
      } else {
        printf("%02x: %02x (%s)\n", i, zi, StringBits(zi).c_str());
        mask |= zi;
      }
    }
  }

  printf("Mask: %s%s" ANSI_RESET "\n",
         ok ? ANSI_GREEN : ANSI_RED, StringBits(mask).c_str());
}

// Long run on N=7 produced this, which has two distinct
// values, so might be close to something good?
[[maybe_unused]]
static half FIF7(half xh) {
  static constexpr std::array<double, 7> arg = {
    32719.28394646058587, // = 32720 (0x77fd)
    32674.43583008654241, // = 32672 (0x77fa)
    32684.92828486524013, // = 32688 (0x77fb)
    41713.42322387963941, // = 41728 (0x7918)
    32720.1946089412013,  // = 32720 (0x77fd)
    50066.62457804620499, // = 50080 (0x7a1d)
    32752.82554916590743, // = 32752 (0x77ff)
  };
  for (int i = 0; i < arg.size(); i++) {
    xh += (half)arg[i];
    xh -= (half)arg[i];
  }

  static constexpr half HALF128 = GetHalf(0x5800);
  xh = HALF128 - xh;

  for (int i = 0; i < arg.size(); i++) {
    xh += (half)arg[i];
    xh -= (half)arg[i];
  }

  return xh;
}


template<size_t N>
half FIF(const std::array<double, N> &arg, half xh) {
  for (int i = 0; i < N; i++) {
    xh += (half)arg[i];
    xh -= (half)arg[i];
  }

  static constexpr half HALF128 = GetHalf(0x5800);
  xh = HALF128 - xh;

  for (int i = 0; i < N; i++) {
    xh += (half)arg[i];
    xh -= (half)arg[i];
  }

  return xh;
}

template<size_t N>
static double IfN(const std::array<double, N> &arg) {
  // Compute the function.
  auto F = [&arg](uint8_t x) {
      return FIF<N>(arg, (half)x);
    };

  int inf_penalty = 0;
  float integer_penalty = 0.0, nonzero_penalty = 0.0;
  // Fewer distinct values is a strong heuristic.
  std::unordered_set<int> distinct;
  for (int x = 0; x < 256; x++) {
    half zh = F(x);
    int zi = (int)zh;
    distinct.insert(zi);
    if (!isfinite(zi)) inf_penalty++;

    // Require integers.
    if (zi != zh) {
      float dz = (float)zh - (float)zi;
      integer_penalty += std::max(0.00125f, dz * dz);
    }

    // We want everything to be zero.
    nonzero_penalty += zh * zh;
  }

  return inf_penalty * 10000000.0 +
    integer_penalty * 100000.0 +
    distinct.size() * 1000.0 +
    nonzero_penalty;
}

// Trying to make a more direct version of "if(cc, v)",
// where cc is either 0 or 1, and we return either 0 or v.
// One idea is if we take ncc = (1 - cc), then we can compute
// offset = ncc * 65000
// v1 = v + offset - offset
// which does nothing when offset is zero, preserving the
// value, but causes a lot of rounding when offset was large.
// if we can get everything to round to zero, then we win!
// so basically we are looking for some series of additions
// that result in zero for everything in [0, 255].
template<size_t N>
static void MakeIf() {
#if 1
  using IfOpt = Optimizer<0, N, char>;

  IfOpt opt([](const IfOpt::arg_type &arg) {
      double err = IfN<N>(arg.second);
      return std::make_pair(err, std::make_optional('x'));
    }, time(nullptr));

  std::array<std::pair<double, double>, N> bounds;
  for (int i = 0; i < N; i++) {
    bounds[i] = make_pair(-257.0, 65472.0);
  }

  opt.Run({}, bounds,
          nullopt, nullopt, {2.0 * 60.0 * 60.0}, {0.0});

  const auto [arg, score, out_] = opt.GetBest().value();
  printf("Best score: %.19g\nParams:\n", score);
  for (int i = 0; i < arg.second.size(); i++) {
    double a = arg.second[i];
    printf("  %.19g = %.11g (0x%04x)\n",
           a, (float)(half)a, GetU16((half)a));
  }

  auto F = [&arg](uint8_t x) {
      half xh = (half)x;
      return FIF<N>(arg.second, xh);
    };

#endif


  /*
  auto F = [](uint8_t x) {
      half xh = (half)x;
      for (uint16_t z : {0x780e, 0x77fd, 0x79f9,
            0x77fb, 0x795c, 0x77fd, 0x7a33, 0x77ff, 0x7800
            }) {
        half h = GetHalf(z);
        xh += h;
        xh -= h;
      }

      xh = 128.0 - xh;

      for (uint16_t z : {0x780e, 0x77fd, 0x79f9,
            0x77fb, 0x795c, 0x77fd, 0x7a33, 0x77ff, 0x7800
            }) {
        half h = GetHalf(z);
        xh += h;
        xh -= h;
      }

      return xh;
    };
  */

  auto StringBits = [](uint8_t zi) {
      string bits(8, '?');
      for (int b = 0; b < 8; b++) {
        bits[b] = (zi & (1 << (7 - b))) ? '1' : '0';
      }
      return bits;
    };

  std::unordered_set<int> distinct;
  bool ok = true;
  for (int i = 0; i < 256; i++) {
    half zh = F(i);
    int zi = zh;
    printf("%02x: ", i);
    if (zh != (half)zi) {
      printf(ARED("%.11g") "\n", (float)zh);
      ok = false;
    } else {
      distinct.insert(zi);
      if (zi < 0 || zi >= 256) {
        printf(ARED("%d") "\n", zi);
        ok = false;
      } else {
        printf("%02x: %02x (%s)\n", i, zi, StringBits(zi).c_str());
      }
    }
  }

  printf("%d distinct elements (%s)\n",
         (int)distinct.size(), ok ? AGREEN("ok") : ARED("invalid"));
}

static void PrintIf() {
  static std::array<half, 8> OFF = {
    GetHalf(0x77f9), GetHalf(0x7829),
    GetHalf(0x77fb), GetHalf(0x78e2),
    GetHalf(0x77fd), GetHalf(0x780b),
    GetHalf(0x77ff), GetHalf(0x7864),
  };

  auto StringBits = [](uint8_t zi) {
      string bits(8, '?');
      for (int b = 0; b < 8; b++) {
        bits[b] = (zi & (1 << (7 - b))) ? '1' : '0';
      }
      return bits;
    };

  for (int i = 0; i < 256; i++) {
    half xh = (half)i;
    for (const half &h : OFF) xh = xh + h - h;
    printf("%02x: %s\n", i, StringBits((int)xh).c_str());
  }
}


int main(int argc, char **argv) {
  AnsiInit();
  // OptimizeShift();
  // MakeShiftAdd();

  // MakeCompress<0x0F>();

  // MakeIf<8>();

  PrintIf();

  return 0;
}
