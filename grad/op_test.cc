#include "expression.h"
#include "timer.h"

#include <algorithm>
#include <functional>
#include <array>
#include <utility>

#include "half.h"

#include "grad-util.h"
#include "makefn-ops.h"
#include "arcfour.h"
#include "randutil.h"

using Table = Exp::Table;
using uint32 = uint32_t;
using uint8 = uint8_t;

static Table MakeTableFromFn(const std::function<half(half)> &f) {
  Table table;
  for (int i = 0; i < 65536; i++) {
    half x = Exp::GetHalf((uint16)i);
    half y = f(x);
    table[i] = Exp::GetU16(y);
  }
  return table;
}

template<size_t N>
auto Sample(ArcFour *rc,
            const std::array<std::pair<double, double>, N> &bounds) ->
  // in [0, 1], then scaled to bounds
  std::pair<std::array<double, N>, std::array<double, N>> {
  std::array<double, N> ret_norm;
  std::array<double, N> ret_scaled;
  for (int i = 0; i < N; i++) {
    double f = RandDouble(rc);
    ret_norm[i] = f;
    const auto [low, high] = bounds[i];
    ret_scaled[i] = low + (high - low) * f;
  }
  return make_pair(ret_norm, ret_scaled);
}

static uint32 MixRGB(float r, float g, float b, float a) {
  uint32 rr = std::clamp((int)(r * 255.0f), 0, 255);
  uint32 gg = std::clamp((int)(g * 255.0f), 0, 255);
  uint32 bb = std::clamp((int)(b * 255.0f), 0, 255);
  uint32 aa = std::clamp((int)(a * 255.0f), 0, 255);
  return (rr << 24) | (gg << 16) | (bb << 8) | aa;
}

static constexpr int IMAGE_SIZE = 1920;

static bool IsZero(const Table &table) {
  double total_sum = 0.0;
  double total_width = 2.0;
  for (half pos = (half)-1.0; pos < (half)1.0; /* in loop */) {
    half next = nextafter(pos, (half)1.0);
    uint16 upos = Exp::GetU16(pos);
    double err = Exp::GetHalf(table[upos]);
    double width = (double)next - (double)pos;
    total_sum += fabs(err) * width;
    pos = next;
  }

  return (total_sum / total_width) < (half)0.001;
}

static void PlotOp2() {
  ArcFour rc("op2");
  // Not used by op2.
  Table target =
    MakeTableFromFn([](half x) {
        return sin(x * (half)3.141592653589);
      });

  ImageRGBA img(IMAGE_SIZE, IMAGE_SIZE);
  img.Clear32(0x000000FF);
  GradUtil::Grid(&img);

  int64 samples_in = 0;
  int64 samples_out = 0;
  int iszero = 0;

  static constexpr int SAMPLES = 5000;
  for (int i = 0; i < SAMPLES; i++) {
    if (i % 1000 == 0) printf("%d/%d\n", i, SAMPLES);
    const auto [norm, scaled] = Sample(&rc, Op2::DOUBLE_BOUNDS);
    auto [rf, gf, bf] = norm;

    const uint32 color =
      MixRGB(rf * 0.9 + 0.1,
             gf * 0.9 + 0.1,
             bf * 0.9 + 0.1, 0.20);

    Exp::Allocator alloc;
    const Exp *exp = Op2::GetExp(&alloc, {}, scaled, target);

    Table result = Exp::TabulateExpression(exp);
    for (int s = 0; s < 256; s++) {
      half x = (half)( (s / 128.0) - 1.0);
      half y = Exp::GetHalf(result[Exp::GetU16(x)]);
      if (y < (half)-1.0 || y > (half)1.0) {
        samples_out++;
      } else {
        samples_in++;
      }
    }

    if (IsZero(result)) iszero++;

    GradUtil::Graph(result, color, &img);
  }
  int64 total_samples = samples_out + samples_in;
  printf("Samples in: %.3f%% out: %.3f%%\n"
         "Zero: %d/%d (%.3f%%)\n",
         (samples_in * 100.0) / total_samples,
         (samples_out * 100.0) / total_samples,
         iszero, SAMPLES, (iszero * 100.0) / SAMPLES);
  img.Save("op2.png");
}

int main(int argc, char **argv) {

  PlotOp2();
  return 0;
}
