

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
static int ForAll(F f) {
  double dist = 0.0;
  for (int x = 0; x < 256; x++) {
    dist += f((uint8_t)x, (half)x);
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


using ShiftOpt = Optimizer<0, 2, char>;


int main(int argc, char **argv) {

  ShiftOpt opt([](const ShiftOpt::arg_type &arg) {
      const auto [fudge, offset] = arg.second;
      double err = TestRightShift1(fudge, offset);
      return std::make_pair(err, std::make_optional('x'));
    }, time(nullptr));

  opt.Run({},
          {
            make_pair(0.45, 0.55),
            make_pair(255.0, 2049.0),
          },
          nullopt, nullopt, {60.0});

  const auto [arg, score, out_] = opt.GetBest().value();
  const auto &[fudge, offset] = arg.second;
  printf("Fudge: %.19g = %.11g (0x%04x)\n"
         "Offset: %.19g = %.11g (0x%04x)\n"
         "score %.19g\n",
         fudge, (float)(half)fudge, GetU16((half)fudge),
         offset, (float)(half)offset, GetU16((half)offset),
         score);

  return 0;
}
