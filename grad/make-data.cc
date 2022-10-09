
#include <string>
#include <cmath>
#include <array>
#include <cstdint>
#include <vector>

#include "base/logging.h"
#include "base/stringprintf.h"

#include "util.h"
#include "image.h"
#include "bounds.h"
#include "opt/optimizer.h"
#include "half.h"
#include "color-util.h"
#include "arcfour.h"
#include "randutil.h"
#include "threadutil.h"

#include "grad-util.h"
#include "ansi.h"

using namespace std;

using Table = GradUtil::Table;
using Step = GradUtil::Step;
using State = GradUtil::State;

static inline float Unpack16AsFloat(uint16 u) {
  return (float)GradUtil::GetHalf(u);
}

static constexpr int IMAGE_SIZE = 1920;

static void MakeData(const std::array<uint16_t, 65536> &table,
                     const string &name,
                     bool force_monotonic) {
  printf("Writing data for " ABLUE("%s") "\n", name.c_str());
  const string forward_file = StringPrintf("forward-%s.png", name.c_str());
  const string deriv_file = StringPrintf("deriv-%s.png", name.c_str());
  const string viz_file = StringPrintf("viz-%s.png", name.c_str());

  GradUtil::SaveFunctionToFile(table, forward_file);

  // Check that the values are preserved round-trip.
  {
    std::vector<uint16> refn =
      GradUtil::GetFunctionFromFile(forward_file);
    for (int i = 0; i < 65536; i++) {
      CHECK(table[i] == refn[i]) << i;
    }
  }

#if 1
  {
  // XXX DELETE - copy of below
  // And debugging/visualization image.
  ImageRGBA img(8192, 8192);
  img.Clear32(0x000000FF);
  GradUtil::Grid(&img);

  double XBOUNDS = 65536.0;
  double YBOUNDS = 8.0;
  // Loop over [-1, 1].
  auto Plot = [&](uint16 input) {
      uint16 output = table[input];
      double x = GradUtil::GetHalf(input);
      double y = GradUtil::GetHalf(output);

      int xs = (int)std::round((img.Width() / 2) + x * (img.Width() / XBOUNDS));
      int ys = (int)std::round((img.Height() / 2) + -y * (img.Width() / YBOUNDS));

      // ys = std::clamp(ys, 0, size - 1);

      uint32 c = 0xFF77FF77;
      /*
      if (x < -1.0f) c = 0xFF000022;
      else if (x > 1.0f) c = 0x00FF0022;
      */
      img.BlendPixel32(xs, ys, c);
    };

    /*
    for (int i = NEG_LOW; i < NEG_HIGH; i++) Plot(i);
    for (int i = POS_LOW; i < POS_HIGH; i++) Plot(i);
    */
    GradUtil::ForEveryFinite16(Plot);


  img.Save("debug.png");
  }
#endif

  // The derivative at a point, given as table mapping the
  // output value (y) to f'(x); this is what we use at training
  // time. Note that this would seemingly require the forward
  // function to be bijective:
  //   (1) It must be "onto", because we want to have a value
  //       for every element in the table. We should only ever
  //       look up values that were actually output by the
  //       forward function (unless using error remapping),
  //       but it is gross to have holes.
  //   (2) It must be "one-to-one", or else the derivative
  //       may be ambiguous. For example, with f(x) = x^2,
  //       f(1) == f(-1), but f'(1) != f'(-1).
  // But the forward function is not bijective:
  //   (1) Many values do not appear in the output (e.g. 0x0002).
  //   (2) Multiple different x are mapped to the same y. (This
  //       is extreme in the version where we first do some
  //       additive shift, creating a flat region below zero.)
  // However, the considered functions are at least monotonic,
  // which means that when they repeat values, they do so
  // consecutively, so the derivative can be considered zero for
  // these points.
  //
  // But also: We don't really want to think of the derivative
  // as zero for most of these, right? In the region that goes
  // 0x0800,0x0801,0x0802,0x0802,0x0803,0x0803,0x0804,0x0805,
  // we probably want to consider this whole thing a shallow
  // straight line, using float precision. So... we should
  // represent the derivative with a piecewise linear approximation?
  //
  // I think a simpler thing for now is to just compute it
  // pointwise, but then apply a lowpass filter for smoothing.
  // We'll do the calculation as doubles, but output floats.

  // Get all the x,y points and sort them by their x coordinates
  // (in float space).
  std::vector<std::pair<uint16, uint16>> points;
  GradUtil::ForEveryFinite16([&table, &points](uint16 x) {
      uint16 y = table[x];
      points.emplace_back(x, y);
    });

  std::sort(points.begin(), points.end(),
            [](auto a, auto b) {
              if (a.first == b.first) {
                float af = Unpack16AsFloat(a.second);
                float bf = Unpack16AsFloat(b.second);
                return af < bf;
              }
              float af = Unpack16AsFloat(a.first);
              float bf = Unpack16AsFloat(b.first);
              return af < bf;
            });

  [[maybe_unused]]
  bool has_negative = false, has_positive = false;
  for (const auto &[x, y] : points) {
    const float fx = Unpack16AsFloat(x);
    const float fy = Unpack16AsFloat(y);
    CHECK(!std::isnan(fx));
    CHECK(!std::isnan(fy));
    if (fy < 0.0) has_negative = true;
    if (fy > 0.0) has_positive = true;
  }

  auto GetPoint = [&points](int i) {
      const auto [x, y] = points[i];
      return std::make_pair(Unpack16AsFloat(x), Unpack16AsFloat(y));
    };

  // Check that the function is monotonic!
  for (int i = 1; i < points.size(); i++) {
    const auto [ax, ay] = points[i - 1];
    const auto [bx, by] = points[i];
    CHECK(Unpack16AsFloat(ax) <= Unpack16AsFloat(bx));
    if (!force_monotonic) {
    CHECK(Unpack16AsFloat(ay) <= Unpack16AsFloat(by)) <<
      StringPrintf("Want %04x <= %04x (%.4f <= %.4f)\n",
                   ay, by, Unpack16AsFloat(ay), Unpack16AsFloat(by));
    }
  }

  // Now compute the derivative at every y value. We do this by
  // looping over y values (they are nondecreasing but may have
  // gaps) and averaging the slope for some window. The window
  // is in point space, because we are trying to smooth out
  // representation imprecision, not lowpass spatially.

  // Indexed by y value.
  // Each point has a list of samples, maybe none.
  std::vector<std::vector<double>> deriv(65536);
  for (int i = 0; i < points.size(); i++) {
    int num_avg = 0;
    double total_avg = 0.0;
    static constexpr int WINDOW_SIZE = 2;
    const auto [xu, yu] = points[i];
    const double x = Unpack16AsFloat(xu);
    const double y = Unpack16AsFloat(yu);
    if (!(std::isfinite(x) && std::isfinite(y)))
      continue;

    for (int j = 1; j < WINDOW_SIZE + 1; j++) {
      for (int p : {i + j, i - j}) {
        if (p >= 0 && p < points.size()) {
          // This forms another point in range.
          const auto [xo, yo] = GetPoint(p);
          if (std::isfinite(xo) && std::isfinite(yo)) {
            // and it is valid.
            const double slope = (yo - y) / (xo - x);
            // ... might still be e.g. 0/0.
            if (std::isfinite(slope)) {
              total_avg += slope;
              num_avg++;
            }
          }
        }
      }
    }

    CHECK(num_avg > 0) << i;
    double avg = total_avg / (double)num_avg;
    CHECK(std::isfinite(avg)) << i << " " << total_avg;
    deriv[yu].push_back(avg);
  }

  auto AverageVec = [](const std::vector<double> &v) {
    CHECK(!v.empty());
    double t = 0.0;
    for (double y : v) t += y;
    double dx = t / v.size();
    CHECK(std::isfinite(dx)) << t << " / " << v.size();
    return dx;
  };

  // Debug dump
  if (false) {
    FILE *f = fopen("deriv-data.txt", "wb");
    CHECK(f);
    for (int y = 0; y < 65536; y++) {
      fprintf(f, "%04x = %.11g:", y, Unpack16AsFloat(y));
      for (double d : deriv[y]) {
        fprintf(f, " %.7f", d);
      }
      fprintf(f, "\n");
    }
    fclose(f);
    printf("Wrote deriv-data.txt\n");
  }

  // Since the code below doesn't cross between positive and negative,
  // if the function never outputs negative values then we won't be
  // able to fill anything in there. This is true for downshift2. So
  // as a hack, fill one value in with something arbitrary.
  if (!has_negative) {
    CHECK(deriv[0x8000].empty());
    deriv[0x8000].push_back(1.0);
  }

  // Now, fill in any gaps. Here we insist on a value for
  // every finite y.
  std::vector<double> deriv_out(65536);
  std::fill(deriv_out.begin(), deriv_out.end(), NAN);
  GradUtil::ForEveryFinite16([&deriv, &deriv_out, &AverageVec](uint16 yu) {
      if (!deriv[yu].empty()) {
        deriv_out[yu] = AverageVec(deriv[yu]);
      } else {
        // Empty. Get the adjacent (in the given direction) bucket that
        // has values, along with the bucket's y coordinate. If there
        // are no non-empty buckets, the coordinate is not meaningful.
        auto GetAdjacentBucket = [&](const uint16 u, const int dir) ->
          pair<uint16, vector<double>> {
          bool verbose = u == 0x3f00; // false; // u == 0x8000; // u == 0x1f01;

          // TODO: Can use NextAfter16?
          // Here we really want like std::nextafter, but instead we
          // just stay in the two contiguous ranges where the values
          // are monotonic. As an improvement, it would be acceptable
          // to go between -0 and 0.
          // Positive is 0-0x7c00. Negative is 0x8000-0xfc00.
          if (u >= 0x0000 && u <= 0x7c00) {
            for (int v = u + dir;
                 v >= 0x0000 && v <= 0x7c00;
                 v += dir) {
              if (!deriv[v].empty()) {
                CHECK(!deriv[v].empty());
                CHECK(deriv[v].size() > 0);
                if (verbose) printf("return 0000-7c00: %d\n", deriv[v].size());
                return std::make_pair(v, deriv[v]);
              }
            }

          } else if (u >= 0x8000 && u <= 0xfc00) {
            for (int v = u + dir;
                 v >= 0x8000 && v <= 0xfc00;
                 v += dir) {
              if (verbose) printf("Try %04x.. %d\n", v, deriv[v].size());
              if (!deriv[v].empty()) {
                if (verbose) printf("return 8000-fc00: %d\n", deriv[v].size());
                return std::make_pair(v, deriv[v]);
              }
            }

          } else {
            CHECK(false) << u;
          }

          // None found.
          if (verbose) printf("%04x %d return none\n", u, dir);
          return std::make_pair(u, std::vector<double>{});
        };

        // The previous y that has a derivative, and the next.
        const auto [pyu, pv] = GetAdjacentBucket(yu, -1);
        const auto [nyu, nv] = GetAdjacentBucket(yu, +1);

        // We should have adjacent points either up or down.
        CHECK(!pv.empty() || !nv.empty()) <<
          StringPrintf("At point: %04x = %.6f\n"
                       "Prev with data: %04x = %.6f\n"
                       "Next with data: %04x = %.6f\n",
                       yu, Unpack16AsFloat(yu),
                       pyu, Unpack16AsFloat(pyu),
                       nyu, Unpack16AsFloat(nyu));

        if (!pv.empty() && !nv.empty()) {
          // If we have both, interpolate.
          double py = Unpack16AsFloat(pyu);
          double ny = Unpack16AsFloat(nyu);
          double y = Unpack16AsFloat(yu);

          double pd = AverageVec(pv);
          double nd = AverageVec(nv);

          // These could be in one order or the other depending
          // on whether we were looping over negative or positive
          // floats.
          if (py <= y && y <= ny) {
            double f = (y - py) / ny - py;
            double dx = std::lerp(pd, nd, f);
            CHECK(std::isfinite(dx)) <<
              StringPrintf(
                  "%04x: lerp(%.3f, %.3f, (%.3f - %.3f) / (%.3f - %.3f)) = %.3f",
                  yu, pd, nd, y, py, ny, py, dx);
            deriv_out[yu] = dx;
          } else if (ny <= y && y <= py) {
            double f = (y - ny) / py - ny;
            double dx = std::lerp(nd, pd, f);
            CHECK(std::isfinite(dx)) <<
              StringPrintf(
                  "%04x: lerp(%.3f, %.3f, (%.3f - %.3f) / (%.3f - %.3f)) = %.3f",
                  yu, nd, pd, y, ny, py, ny, dx);
            deriv_out[yu] = dx;
          } else {
            CHECK(false) << "y points should be ordered";
          }
        } else if (!pv.empty()) {
          // Otherwise just copy the previous point.
          deriv_out[yu] = AverageVec(pv);
        } else {
          // ... or next point.
          CHECK(!nv.empty());
          deriv_out[yu] = AverageVec(nv);
        }
      }
    });

  GradUtil::ForEveryFinite16([&deriv_out](uint16 yu) {
      CHECK(std::isfinite(deriv_out[yu])) <<
        StringPrintf("%04x: %.3f\n", yu, deriv_out[yu]);
    });

  printf("  deriv at " AYELLOW("0xfbff") " (min finite): " APURPLE("%.6f") "\n"
         "  deriv at " AYELLOW("0x7bff") " (max finite): " APURPLE("%.6f") "\n",
         deriv_out[0xfbff],
         deriv_out[0x7bff]);

  // Extrapolate derivative at -inf and +inf. This keeps
  // infinite errors from becoming nans (and it is easy
  // to saturate at infinity with the max finite value
  // being 65504!)
  deriv_out[0xfc00] = deriv_out[0xfbff];
  deriv_out[0x7c00] = deriv_out[0x7bff];


  // Output derivative.
  ImageRGBA deriv_img(256, 256);
  // int nonzero = 0;
  for (int i = 0; i < 65536; i++) {
    int y = i / 256;
    int x = i % 256;
    float f = deriv_out[i];
    uint32 u = GradUtil::PackFloat(f);
    // if (u) nonzero++;
    deriv_img.SetPixel32(x, y, u);
  }
  // printf("Write nonzero: %d\n", nonzero);
  deriv_img.Save(deriv_file);

  // Test that it's round-trip clean
  std::vector<float> rederiv =
    GradUtil::GetDerivativeFromFile(deriv_file);
  CHECK(rederiv.size() == 65536);
  for (int i = 0; i < 65536; i++) {
    float f1 = deriv_out[i];
    float f2 = rederiv[i];
    uint32 u1 = GradUtil::PackFloat(f1);
    uint32 u2 = GradUtil::PackFloat(f2);
    CHECK(u1 == u2) << i << ": " << u1 << " " << u2
                    << " from " << f1 << " " << f2;
  }


  // And debugging/visualization image.
  ImageRGBA img(IMAGE_SIZE, IMAGE_SIZE);
  img.Clear32(0x000000FF);
  GradUtil::Grid(&img);
  GradUtil::Graph(table, 0xFFFFFF77, &img);

  // Graph derivative within this range.
  double max_mag = 0.0;
  GradUtil::ForPosNeg1([&deriv_out, &max_mag](uint16 u) {
      if (std::isfinite(deriv_out[u]))
        max_mag = std::max(std::abs(deriv_out[u]), max_mag);
    });

  // But clip extreme magnitudes.
  max_mag = std::min(max_mag, 2.0);

  const double scale = ((IMAGE_SIZE / 2.0) * 0.9) / max_mag;
  GradUtil::ForNeg1To1Ascending([&table, &img, &deriv_out, scale](uint16 yu) {
      // Or plot some error pixels. But we assert this above.
      if (!std::isfinite(deriv_out[yu])) return;

      // Scaled
      double y = Unpack16AsFloat(yu);
      int ys = (int)std::round((IMAGE_SIZE / 2) + -y * (IMAGE_SIZE / 2.0));

      double d = deriv_out[yu] * scale;
      int ds = (int)std::round((IMAGE_SIZE / 2) + d);
      img.BlendPixel32(ds, ys, 0xFFFF0077);
      });

  // Also plot the running integral of the derivative, to see that it's
  // close to the original function. Start with the value immediately
  // less than -1.
  double yint = Unpack16AsFloat(table[0xbc01]);
  double prevx = Unpack16AsFloat(0xbc01);
  GradUtil::ForNeg1To1Ascending(
      [&table, &img, &deriv_out, &yint, &prevx](uint16 xu) {

      const double x = Unpack16AsFloat(xu);
      CHECK(x >= prevx) << StringPrintf("want %.11g >= %.11g\n", x, prevx);
      const double dx = x - prevx;
      // Derivative table is indexed by f(x), not x.
      const uint16 yu = table[xu];
      const double dy = deriv_out[yu];

      if (dx > 0.0)
        yint += dy * dx;

      int xs = (int)std::round((IMAGE_SIZE / 2.0) + x * (IMAGE_SIZE / 2.0));
      int ys = (int)std::round((IMAGE_SIZE / 2.0) - yint * (IMAGE_SIZE / 2.0));
      img.BlendPixel32(xs, ys, 0xFF000033);

      prevx = x;
    });

  string md = StringPrintf("%0.5f", max_mag);
  img.BlendText32(IMAGE_SIZE - md.size() * 9 - 6, 2, 0xFFFF0077,
                  md);

  img.Save(viz_file);
  printf("Wrote viz to " AGREEN("%s") "\n", viz_file.c_str());
}

int main(int argc, char **argv) {
  AnsiInit();

  if (true) {
    State state = GradUtil::MakeTable1();
    MakeData(state.table, "grad1", false);
  }

  {
    Table table;
    for (int i = 0; i < 65536; i++) {
      uint16 out = i >> 2;
      table[i] = out;
    }
    MakeData(table, "downshift2", true);
  }

  return 0;
}
