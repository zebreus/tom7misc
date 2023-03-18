#include "textsvg.h"

#include <unordered_set>

#include "util.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "bounds.h"
#include "ansi.h"

#include "grad-util.h"
#include "expression.h"

#include "half.h"

using half_float::half;

static inline NextAfter16(uint16_t pos) {
  // Zero comes immediately after -0.
  if (pos == 0x8000) return 0x0000;
  else if (pos > 0x8000) return pos - 1;
  else return pos + 1;
}

static inline NextAfterHalf(half h) {
  return GradUtil::GetHalf(NextAfter16(GradUtil::GetU16(h)));
}

static void HalfStats() {
  int num_finite = 0;
  GradUtil::ForEveryFinite16([&](uint16_t u) {
    num_finite++;
  });
  printf("There are " AWHITE("%d") " finite half-precision values.\n",
         num_finite);
}

template<class F>
static void ComputeStats(const string &name, const F &f) {
  half min = (half)f((half)0.0f);
  half max = min;

  std::unordered_set<uint16_t> distinct_ival;
  GradUtil::ForNeg1To1Ascending([&](uint16_t u) {
    half h = GradUtil::GetHalf(u);
    half r = (half)f(h);
    uint16_t ru = GradUtil::GetU16(r);
    distinct_ival.insert(ru);
    });

  // assumes finite...
  half minf = (half)f((half)0.0f);
  half maxf = min;
  int num_finite = 0;
  GradUtil::ForEveryFinite16([&](uint16_t u) {
    num_finite++;
    half h = GradUtil::GetHalf(u);
    half r = (half)f(h);
    if (r < min) min = r;
    if (r > max) max = r;
    if (isfinite(r)) {
      if (r < minf) minf = r;
      if (r > maxf) maxf = r;
    }
    });
  printf(ACYAN("%s")
         "  " AWHITE("|") "  "
         " min " ARED("%.9g") " max " AGREEN("%.9g")
         "  " AWHITE("|") "  "
         " min-fin " ARED("%.9g") " max-fin " AGREEN("%.9g")
         "\n   "
         " [-1,1] distinct " APURPLE("%d") " (" AWHITE("%.3f%%") ")"
         "\n",
         name.c_str(),
         (double)min, (double)max,
         (double)minf, (double)maxf,
         (int)distinct_ival.size(),
         (100.0 * distinct_ival.size()) / (double)num_finite);
}

template<class F>
static void PlotSVG(const string &outfile,
                    const F &f,
                    double xlo, double xhi,
                    double ylo, double yhi,
                    // nominally, inches
                    double svg_width, double svg_height,
                    // e.g. in 1/DPI
                    double precision = 0.00001) {
  ComputeStats(outfile, f);


  Bounds bounds;
  bounds.Bound(xlo, ylo);
  bounds.Bound(xhi, yhi);
  // margin?

  double px_width = svg_width * 300.0;
  double px_height = svg_height * 300.0;

  Bounds::Scaler scaler = bounds.ScaleToFit(px_width, px_height, true).FlipY();

  string out = TextSVG::HeaderEx(0.0, 0.0, px_width, px_height,
                                 "px",
                                 "plot.cc");

  auto XC = [&scaler](double x) { return TextSVG::Rtos(scaler.ScaleX(x)); };
  auto YC = [&scaler](double y) { return TextSVG::Rtos(scaler.ScaleY(y)); };

  // X axis
  StringAppendF(&out, "<line x1=\"%s\" y1=\"%s\" x2=\"%s\" y2=\"%s\" "
                "stroke=\"black\" />\n",
                // Really should use screen bounds for this
                XC(xlo).c_str(), YC(0.0).c_str(),
                XC(xhi).c_str(), YC(0.0).c_str());

  // Y axis
  StringAppendF(&out, "<line x1=\"%s\" y1=\"%s\" x2=\"%s\" y2=\"%s\" "
                "stroke=\"black\" />\n",
                // Really should use screen bounds for this
                XC(0.0).c_str(), YC(ylo).c_str(),
                XC(0.0).c_str(), YC(yhi).c_str());

  // Now the data points. We generate the data series and then
  // smooth using the TextSVG util.
  std::vector<std::pair<double, double>> points;
  uint16_t ux = GradUtil::GetU16((half)xlo);
  const uint16_t ux_end = GradUtil::GetU16((half)xhi);
  while (ux != ux_end) {
    half x = GradUtil::GetHalf(ux);
    auto y = f(x);
    points.emplace_back((double)x, (double)y);
    ux = NextAfter16(ux);
    /*
    CHECK(ox != x) << StringPrintf("%04x %04x",
                                   GradUtil::GetU16(ox),
                                   GradUtil::GetU16(x));
    */
    /*
    if (points.size() % 1000 == 0)
      printf("Now %lld\n", (int64_t)points.size());
    */
  }

  if (precision > 0.0)
    points = TextSVG::RemoveColinear(points, precision);

  StringAppendF(&out, "<polyline points=\"");
  for (const auto &[x, y] : points) {
    StringAppendF(&out, "%s,%s ", XC(x).c_str(), YC(y).c_str());
  }
  StringAppendF(&out, "\" fill=\"none\" stroke=\"red\" />\n");

  out += TextSVG::Footer();
  Util::WriteFile(outfile, out);
  printf("Wrote %s\n", outfile.c_str());
}

/*
static double Tanh(half h) {
  return tanh(h);
}
*/
int main(int argc, char **argv) {
  AnsiInit();

  HalfStats();

  Exp::Allocator alloc;

  PlotSVG("tanh.svg",
          [](half h) { return tanh(h); },
          // Tanh,
          -2.0, 2.0,
          -1.0, 1.0,
          4.0, 2.0);

  PlotSVG("relu.svg",
          [](double h) { return h < 0.0 ? 0.0 : h; },
          -1.0, 1.0,
          -1.0, 1.0,
          2.0, 2.0);

  // TODO: A version that skips discontinuities?
  PlotSVG("plus128.svg",
          [](half h) { return h + 128.0_h - 128.0_h; },
          -1.0, 1.0,
          -1.0, 1.0,
          2.0, 2.0);

  PlotSVG("times128.svg",
          [](half h) { return h * 128.0_h * (1.0_h / 128.0_h); },
          -1.0, 1.0,
          -1.0, 1.0,
          2.0, 2.0,
          0.00000001);

  /*
  PlotSVG("times128corner.svg",
          [](half h) { return h * 128.0_h * (1.0_h / 128.0_h); },
          1.0 - 0.0625 - 2, 1.0 - 0.0625,
          1.0 - 0.0625 * 2, 1.0 - 0.0625,
          2.0, 2.0,
          0.0);
  */

  PlotSVG("times100.svg",
          [](half h) { return h * 100.0_h * GradUtil::GetHalf(0x211f); },
          -1.0, 1.0,
          -1.0, 1.0,
          2.0, 2.0,
          0.0001);

  PlotSVG("times100corner.svg",
          [](half h) { return h * 100.0_h * GradUtil::GetHalf(0x211f); },
          1.0 - 0.0625 - 2, 1.0 - 0.0625,
          1.0 - 0.0625 * 2, 1.0 - 0.0625,
          2.0, 2.0,
          0.0);

  /*
  PlotSVG("times128corner.svg",
          [](half h) { return h * 128.0_h * (1.0_h / 128.0_h); },
          1.0 - 0.0625 - 2, 1.0 - 0.0625,
          1.0 - 0.0625 * 2, 1.0 - 0.0625,
          2.0, 2.0,
          0.0);
  */


  const auto table1 = GradUtil::MakeTable1();

  PlotSVG("grad1.svg",
          [&table1](half h) {
            return GradUtil::GetHalf(table1.table[GradUtil::GetU16(h)]);
          },
          -1.0, 1.0,
          -1.0, 1.0,
          2.0, 2.0);

  PlotSVG("grad1minusx.svg",
          [&table1](half h) {
            return GradUtil::GetHalf(table1.table[GradUtil::GetU16(h)]) - h;
          },
          -1.0, 1.0,
          -0.2, 0.2,
          2.0, 0.4);

  string error;
  const Exp *perm16good2 =
    Exp::Deserialize(
        &alloc,
        Util::ReadFile("perm16good2.txt"),
        &error);
  CHECK(perm16good2 != nullptr) << error;

  printf(AWHITE("perm16good2") " is " AYELLOW("%d") " operations.\n",
         Exp::ExpSize(perm16good2));
  PlotSVG("perm16good2.svg",
          [perm16good2](half h) {
            return GradUtil::GetHalf(
                Exp::EvaluateOn(perm16good2,
                                GradUtil::GetU16(h)));
          },
          -1, 1,
          -1.1, 0.9,
          2.0, 2.0);

  const Exp *square =
    Exp::Deserialize(
        &alloc,
        Util::ReadFile("square.txt"),
        &error);
  CHECK(square != nullptr) << error;

  printf(AWHITE("square") " is " AYELLOW("%d") " operations.\n",
         Exp::ExpSize(square));
  PlotSVG("square.svg",
          [square](half h) {
            return GradUtil::GetHalf(
                Exp::EvaluateOn(square,
                                GradUtil::GetU16(h)));
          },
          -2, 2,
          -0.375, 3.575,
          4.0, 4.0);

  /*
  PlotSVG("downshift2.svg",
          [&table1](half h) {
            return GradUtil::GetHalf(GradUtil::GetU16(h) >> 2);
          },
          // maybe from [-0.5 to 0.5]?
          -1.0, 1.0,
          -0.05, 0.15,
          2.0, 0.2);
  */

  PlotSVG("downshift2.svg",
          [&table1](half h) {
            return GradUtil::GetHalf(GradUtil::GetU16(h) >> 2);
          },
          // maybe from [-0.5 to 0.5]?
          -0.5, 0.5,
          -0.05, 0.15,
          1.0, 0.2);

  PlotSVG("boxcar25.svg",
          [&table1](half h) {
            return (h + (half)0.25f) - h;
          },
          -1, 1,
          0.25 - 0.0125, 0.25 + 0.0125,
          1, 0.0125 * 2);

  PlotSVG("identity.svg",
          [&table1](half h) {
            return h;
          },
          -1, 1,
          -1, 1,
          2.0, 2.0);

  return 0;
}
