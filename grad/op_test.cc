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
#include "color-util.h"

using Table = Exp::Table;
using uint32 = uint32_t;
using uint8 = uint8_t;

template<size_t N>
auto Sample(ArcFour *rc,
            const std::array<std::pair<int, int>, N> &bounds) ->
  // in [0, 1], then integer in bounds
  std::pair<std::array<double, N>, std::array<int, N>> {
  std::array<double, N> ret_norm;
  std::array<int, N> ret_scaled;
  for (int i = 0; i < N; i++) {
    // Note int bounds are [low, high).
    const auto [low, high] = bounds[i];
    int offset = RandTo(rc, high - low);
    ret_scaled[i] = low + offset;
    ret_norm[i] = offset / (double)(high - low - 1);
  }
  return make_pair(ret_norm, ret_scaled);
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
  return ColorUtil::FloatsTo32(r, g, b, a);
}

static constexpr int IMAGE_SIZE = 1920;

static bool IsZero(const Table &table) {
  double total_sum = 0.0;
  double total_width = 2.0;
  uint16 ulow = Exp::GetU16((half)-1.0);
  uint16 uhigh = Exp::GetU16((half)+1.0);
  for (uint16 upos = ulow; upos != uhigh; /* in loop */) {
    uint16 unext = Exp::NextAfter16(upos);
    double err = Exp::GetHalf(table[upos]);
    // Same problem with Error here (point at -0 has zero width).
    double width = (double)Exp::GetHalf(unext) - (double)Exp::GetHalf(upos);
    total_sum += fabs(err) * width;
    upos = unext;
  }

  return (total_sum / total_width) < (half)0.001;
}

struct Stats {
  Stats() {}

  int64 samples_in = 0;
  int64 samples_out = 0;
  int iszero = 0;
  int64 denom = 0;

  void Accumulate(const Table &result) {
    denom++;
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
  }

  void Report() {
    int64 total_samples = samples_out + samples_in;
    printf("Samples in: %.3f%% out: %.3f%%\n"
           "Zero: %d/%d (%.3f%%)\n",
           (samples_in * 100.0) / total_samples,
           (samples_out * 100.0) / total_samples,
           iszero, denom, (iszero * 100.0) / denom);
  }

};

static void PlotOp2() {
  ArcFour rc("op2");
  // Not used by op2.
  Table target =
    Exp::MakeTableFromFn([](half x) {
        return sin(x * (half)3.141592653589);
      });

  ImageRGBA img(IMAGE_SIZE, IMAGE_SIZE);
  img.Clear32(0x000000FF);
  GradUtil::Grid(&img);

  static constexpr int SAMPLES = 5000;
  Stats stats;
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
    stats.Accumulate(result);

    GradUtil::Graph(result, color, &img);
  }
  stats.Report();
  img.Save("op2.png");
}

static void PlotShift() {
  ImageRGBA img(IMAGE_SIZE, IMAGE_SIZE);
  img.Clear32(0x000000FF);
  GradUtil::Grid(&img);

  Table upresult;
  Table scaled_upresult;
  Table dnresult;
  Table scaled_dnresult;
  Table dn2result;
  Table dn2seresult;
  for (int i = 0; i < 65536; i++) {
    {
      uint16 upout = i << 1;
      upresult[i] = upout;
      half h = Exp::GetHalf(upout) / (half)32768.0;
      scaled_upresult[i] = Exp::GetU16(h);
    }

    {
      uint16 downout = i >> 1;
      dnresult[i] = downout;
      half h = Exp::GetHalf(downout) / (half)-512.0;
      scaled_dnresult[i] = Exp::GetU16(h);
    }

    {
      uint16 down2out = i >> 2;
      dn2result[i] = down2out;
    }

    {
      // With sign extension.
      uint16 down2seout = (i >> 2);
      if (i & 0x8000) down2seout |= 0xC000;
      dn2seresult[i] = down2seout;
    }

  }

  int ypos = 2;
  auto Add = [&](const string &name, const Table &table, uint32 color) {
      GradUtil::Graph(table, color, &img);
      img.BlendText2x32(2, ypos, color, name);
      ypos += 20;
    };

  /*
  Add("up", upresult, 0xAAAAFFAA);
  Add("up, scaled", scaled_upresult, 0x3333FFAA);
  Add("down", dnresult, 0xAAFFAAAA);
  Add("down, scaled", scaled_dnresult, 0x33FF33AA);
  */

  // shifting down by two seems to be a sweet spot that does not
  // require any scaling to be reasonable (because down-shifting
  // is basically scaling once the sign bit is in the exponent.
  // Tom 7 approved!
  Add("down2", dn2result, 0xFFAAAAAA);
  // Add("down2, sign extended", dn2seresult, 0xFFAAFFAA);

  img.Save("op-shift.png");
}


static double StrobeOffset(std::pair<double, double> bound,
                           int s, int num_strobe, double frac) {
  auto [low, high] = bound;

  // full width of strobe
  double w = (high - low) * frac;
  // half width of strobe (+/-)
  double hw = w * 0.5;
  // magnitude per strobe sample (on each side)
  double m = hw / num_strobe;

  double r = m * (s >> 1);
  return (s & 1) ? r : -r;
}

static int StrobeOffsetI(int s) {
  return (s & 1) ? (s >> 1) : -(s >> 1);
}

static void PlotOp3() {
  ArcFour rc("op3");
  // Not used by op3.
  Table target =
    Exp::MakeTableFromFn([](half x) {
        return sin(x * (half)3.141592653589);
      });

  ImageRGBA img(IMAGE_SIZE, IMAGE_SIZE);
  img.Clear32(0x000000FF);
  GradUtil::Grid(&img);

  static constexpr int STROBE = 100;
  // static constexpr double STROBE_FRAC = 0.1;
  static constexpr int SAMPLES = 50;
  static constexpr int STROBE_DIM = 0;
  Stats stats;
  for (int i = 0; i < SAMPLES; i++) {
    if (i % 1000 == 0) printf("%d/%d\n", i, SAMPLES);
    const auto [inorm, iscaled] = Sample(&rc, Op3::INT_BOUNDS);
    const auto [dnorm, dscaled] = Sample(&rc, Op3::DOUBLE_BOUNDS);
    auto [iterf] = inorm;
    auto [xf, yf, zf, wf] = dnorm;

    for (int s = 0; s < STROBE; s++) {
      auto iscaledo = iscaled;
      auto dscaledo = dscaled;
      /*
      double d = StrobeOffset(Op3::DOUBLE_BOUNDS[STROBE_DIM],
                              s, STROBE, STROBE_FRAC);
      dscaledo[STROBE_DIM] += d;
      */

      int d = StrobeOffsetI(s);
      iscaledo[STROBE_DIM] += d;
      if (iscaledo[STROBE_DIM] < Op3::INT_BOUNDS[STROBE_DIM].first ||
          iscaledo[STROBE_DIM] >= Op3::INT_BOUNDS[STROBE_DIM].second)
        continue;

      // pretty arbitrary 5D -> 3D reduction
      const auto [r1, g1, b1] =
        ColorUtil::HSVToRGB(iterf, 0.1 + xf * 0.9, 0.1 + yf * 0.9);
      const auto [r2, g2, b2] =
        ColorUtil::HSVToRGB(zf, 0.1 + wf * 0.9, 1.0);

      const float alpha = 0.05 + 0.15 * ((STROBE - s) / (float)STROBE);

      const uint32 color =
        MixRGB((r1 + r2) * 0.5, (g1 + g2) * 0.5, (b1 + b2) * 0.5,
               alpha);

      Exp::Allocator alloc;
      const Exp *exp = Op3::GetExp(&alloc, iscaledo, dscaledo, target);

      Table result = Exp::TabulateExpression(exp);
      stats.Accumulate(result);

      GradUtil::Graph(result, color, &img);
    }
  }
  stats.Report();
  img.Save("op3.png");
}

static void PlotOp4() {
  ArcFour rc("op4");

  Table target =
    Exp::MakeTableFromFn([](half x) {
        return sin(x * (half)3.141592653589);
      });

  ImageRGBA img(IMAGE_SIZE, IMAGE_SIZE);
  img.Clear32(0x000000FF);
  GradUtil::Grid(&img);

  static constexpr int STROBE = 20;
  static constexpr double STROBE_FRAC = 0.1;
  static constexpr int SAMPLES = 50;
  static constexpr int STROBE_DIM = 1;
  Stats stats;
  for (int i = 0; i < SAMPLES; i++) {
    if (i % 1000 == 0) printf("%d/%d\n", i, SAMPLES);
    const auto [inorm, iscaled] = Sample(&rc, Op4::INT_BOUNDS);
    const auto [dnorm, dscaled] = Sample(&rc, Op4::DOUBLE_BOUNDS);
    auto [a, b, c] = inorm;
    auto [d, e] = dnorm;

    for (int s = 0; s < STROBE; s++) {
      auto iscaledo = iscaled;
      auto dscaledo = dscaled;

      double d = StrobeOffset(Op4::DOUBLE_BOUNDS[STROBE_DIM],
                              s, STROBE, STROBE_FRAC);
      dscaledo[STROBE_DIM] += d;

      /*
      int d = StrobeOffsetI(s);
      iscaledo[STROBE_DIM] += d;
      if (iscaledo[STROBE_DIM] < Op4::INT_BOUNDS[STROBE_DIM].first ||
          iscaledo[STROBE_DIM] >= Op4::INT_BOUNDS[STROBE_DIM].second)
        continue;
      */

      // pretty arbitrary 5D -> 3D reduction
      const auto [r1, g1, b1] =
        ColorUtil::HSVToRGB(a, 0.1 + b * 0.9, 0.1 + c * 0.9);
      const auto [r2, g2, b2] =
        ColorUtil::HSVToRGB(d, 0.1 + e * 0.9, 1.0);

      const float alpha = 0.05 + 0.15 * ((STROBE - s) / (float)STROBE);

      const uint32 color =
        MixRGB((r1 + r2) * 0.5, (g1 + g2) * 0.5, (b1 + b2) * 0.5,
               alpha);

      Exp::Allocator alloc;
      const Exp *exp = Op4::GetExp(&alloc, iscaledo, dscaledo, target);

      Table result = Exp::TabulateExpression(exp);
      stats.Accumulate(result);

      GradUtil::Graph(result, color, &img);
    }
  }
  stats.Report();
  img.Save("op4.png");
}

static void PlotOp5() {
  ArcFour rc("op5");

  Table target =
    Exp::MakeTableFromFn([](half x) {
        return sin(x * (half)3.141592653589);
      });

  ImageRGBA img(IMAGE_SIZE, IMAGE_SIZE);
  img.Clear32(0x000000FF);
  GradUtil::Grid(&img);

  static constexpr int STROBE = 50;
  static constexpr double STROBE_FRAC = 0.25;
  static constexpr int SAMPLES = 50;
  static constexpr int STROBE_DIM = 1;
  Stats stats;
  for (int i = 0; i < SAMPLES; i++) {
    if (i % 1000 == 0) printf("%d/%d\n", i, SAMPLES);
    const auto [inorm, iscaled] = Sample(&rc, Op5::INT_BOUNDS);
    const auto [dnorm, dscaled] = Sample(&rc, Op5::DOUBLE_BOUNDS);
    auto [a, b] = inorm;
    auto [c, d, e] = dnorm;

    for (int s = 0; s < STROBE; s++) {
      auto iscaledo = iscaled;
      auto dscaledo = dscaled;

      double d = StrobeOffset(Op5::DOUBLE_BOUNDS[STROBE_DIM],
                              s, STROBE, STROBE_FRAC);
      dscaledo[STROBE_DIM] += d;

      /*
      int d = StrobeOffsetI(s);
      iscaledo[STROBE_DIM] += d;
      if (iscaledo[STROBE_DIM] < Op5::INT_BOUNDS[STROBE_DIM].first ||
          iscaledo[STROBE_DIM] >= Op5::INT_BOUNDS[STROBE_DIM].second)
        continue;
      */

      // pretty arbitrary 5D -> 3D reduction
      const auto [r1, g1, b1] =
        ColorUtil::HSVToRGB(a, 0.1 + b * 0.9, 0.1 + c * 0.9);
      const auto [r2, g2, b2] =
        ColorUtil::HSVToRGB(d, 0.1 + e * 0.9, 1.0);

      const float alpha = 0.05 + 0.15 * ((STROBE - s) / (float)STROBE);

      const uint32 color =
        MixRGB((r1 + r2) * 0.5, (g1 + g2) * 0.5, (b1 + b2) * 0.5,
               alpha);

      Exp::Allocator alloc;
      const Exp *exp = Op5::GetExp(&alloc, iscaledo, dscaledo, target);

      Table result = Exp::TabulateExpression(exp);
      stats.Accumulate(result);

      GradUtil::Graph(result, color, &img);
    }
  }
  stats.Report();
  img.Save("op5.png");
}

// A nice discontinuous function; can we move it around?
static void StrobeChoppy5() {

  Table target =
    Exp::MakeTableFromFn([](half x) {
        return (half)0.0;
      });

  ImageRGBA img(IMAGE_SIZE, IMAGE_SIZE);
  img.Clear32(0x000000FF);
  GradUtil::Grid(&img);

  static constexpr int STROBE = 50;
  static constexpr double STROBE_FRAC = 0.01;
  Stats stats;

  const std::array<int, 2> BASE_INTS = {49758, 49152};
  const std::array<double, 3> BASE_DOUBLES =
    {0.0039, -3.9544, 0.0760};

  Exp::Allocator alloc;

  auto MakeExp = [&](int a, int b,
                     double x, double y) {
      const double z = 0.0;
      const std::array<int, 2> INTS = {a, b};
      const std::array<double, 3> DOUBLES = {x, y, z};

      return
        alloc.TimesC(
            Op5::GetExp(&alloc, INTS, DOUBLES, target),
            Exp::GetU16((half)32.0));
    };

  for (int s = 0; s < STROBE; s++) {
    auto [i1, i2] = BASE_INTS;
    auto [d1, d2, d3_] = BASE_DOUBLES;

    [[maybe_unused]]
    double d = StrobeOffset(Op5::DOUBLE_BOUNDS[0],
                            s, STROBE, STROBE_FRAC);
    [[maybe_unused]]
    int di = StrobeOffsetI(s) * 2;
    const Exp *exp = MakeExp(i1, i2 + di, d1, d2);

    const float alpha = 0.05 + 0.15 * ((STROBE - s) / (float)STROBE);

    const uint32 color = MixRGB(alpha, 0.5, 1.0, alpha);


    Table result = Exp::TabulateExpression(exp);
    stats.Accumulate(result);

    GradUtil::Graph(result, color, &img, di);
  }

  stats.Report();

  {
    // original
    auto [i1, i2] = BASE_INTS;
    auto [d1, d2, d3_] = BASE_DOUBLES;
    const Exp *exp = MakeExp(i1, i2, d1, d2);
    Table result = Exp::TabulateExpression(exp);
    GradUtil::Graph(result, 0xFFFFFFAA, &img, 0);
    GradUtil::Graph(result, 0xFFFFFFAA, &img, 1);
    printf("%s\n", Exp::ExpString(exp).c_str());

    for (int i = 0; i < 16; i++) {
      half x = (half)((i / (double)8) - 1.0);
      x += (half)(1.0/32.0);

      half y = Exp::GetHalf(Exp::EvaluateOn(exp, Exp::GetU16(x)));

      double yi = ((double)y + 1.0) * 8.0;
      int ypos =
        // put above the line
        -16 +
        // 0 is the center
        (IMAGE_SIZE / 2) +
        (double)-y * (IMAGE_SIZE / 2);
      img.BlendText32(i * (IMAGE_SIZE / 16) + 8,
                      ypos,
                      0xFFFFFFAA,
                      StringPrintf("%.5f", yi));
    }
  }

  img.Save("chop5.png");
}


int main(int argc, char **argv) {
  /*
  PlotOp2();
  PlotOp3();
  PlotOp4();
  PlotOp5();
  */

  PlotShift();

  // StrobeChoppy5();
  return 0;
}
