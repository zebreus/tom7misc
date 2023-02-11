

#include <cstdint>
#include <cstdio>

#include "half.h"
#include "opt/optimizer.h"

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


using ShiftOpt = Optimizer<0, 3, char>;


int main(int argc, char **argv) {

  ShiftOpt opt([](const ShiftOpt::arg_type &arg) {
      const auto [a, b, c] = arg.second;
      double err = TestRightShift7(a, b, c);
      return std::make_pair(err, std::make_optional('x'));
    }, time(nullptr));

  opt.Run({},
          {
            // make_pair(-0.51, 0.51),
            make_pair(1.0 / 300.0, 1.0 / 60.0),
            make_pair(255.0, 2049.0),
            make_pair(255.0, 2049.0),
          },
          nullopt, nullopt, {60.0}, {0.0});

  const auto [arg, score, out_] = opt.GetBest().value();
  const auto &[a, b, c] = arg.second;
  printf("a: %.19g = %.11g (0x%04x)\n"
         "b: %.19g = %.11g (0x%04x)\n"
         "c: %.19g = %.11g (0x%04x)\n"
         "score %.19g\n",
         a, (float)(half)a, GetU16((half)a),
         b, (float)(half)b, GetU16((half)b),
         c, (float)(half)c, GetU16((half)c),
         score);

  return 0;
}
