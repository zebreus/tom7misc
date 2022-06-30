
#include <string>
#include <cmath>

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

using namespace std;

using Table = GradUtil::Table;
using Step = GradUtil::Step;
using State = GradUtil::State;

static inline float UnpackFloat(uint16 u) {
  return (float)GradUtil::GetHalf(u);
}

static constexpr int IMAGE_SIZE = 1920;

// 500 iterations of multiplying by 0.99951171875, which
// is the first number smaller than one.
static State MakeTable1() {
  static constexpr uint16 C = 0x3bffu;
  static constexpr int ITERS = 500;
  State state;
  for (int i = 0; i < ITERS; i++) {
    GradUtil::ApplyStep(Step{.mult = true, .value = C}, &state.table);
  }

  // And recenter.
  const auto &[offset, scale] = GradUtil::Recentering(state.table);
  // printf("Offset %.9g Scale %.9g\n", (float)offset, (float)scale);
  GradUtil::ApplyStep(Step{.mult = false,
                           .value = GradUtil::GetU16(offset)},
    &state.table);
  GradUtil::ApplyStep(Step{.mult = true,
                           .value = GradUtil::GetU16(scale)},
    &state.table);
  return state;
}

int main(int argc, char **argv) {
  State state = MakeTable1();

  ImageRGBA forward(256, 256);
  for (int i = 0; i < 65536; i++) {
    uint16_t o = state.table[i];
    uint8_t r = (o >> 8) & 255;
    uint8_t g = o & 255;
    int y = i >> 8;
    int x = i & 255;
    forward.SetPixel(x, y, r, g, 0x00, 0xFF);
  }
  forward.Save("forward.png");

  // The derivative at a point, given as table mapping the
  // output value (y) to f'(x); this is what we use at training
  // time. Note that this would seemling require the forward
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

  std::vector<std::pair<uint16, uint16>> points;
  GradUtil::ForEveryFinite16([&state, &points](uint16 x) {
      uint16 y = state.table[x];
      points.emplace_back(x, y);
    });

  std::sort(points.begin(), points.end(),
            [](auto a, auto b) {
              if (a.first == b.first) {
                float af = UnpackFloat(a.second);
                float bf = UnpackFloat(b.second);
                return af < bf;
              }
              float af = UnpackFloat(a.first);
              float bf = UnpackFloat(b.first);
              return af < bf;
            });

  for (const auto &[x, y] : points) {
    CHECK(!std::isnan(UnpackFloat(x)));
    CHECK(!std::isnan(UnpackFloat(y)));
  }

  auto GetPoint = [&points](int i) {
      const auto [x, y] = points[i];
      return std::make_pair(UnpackFloat(x), UnpackFloat(y));
    };

  // Check that the function is monotonic!
  for (int i = 1; i < points.size(); i++) {
    const auto [ax, ay] = points[i - 1];
    const auto [bx, by] = points[i];
    CHECK(UnpackFloat(ax) <= UnpackFloat(bx));
    CHECK(UnpackFloat(ay) <= UnpackFloat(by));
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
    const double x = UnpackFloat(xu);
    const double y = UnpackFloat(yu);
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
    return t / v.size();
  };

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
        // are no-non-empty buckets, the coordinate is not meaningful.
        auto GetAdjacentBucket = [&](uint16 u, int dir) ->
          pair<uint16, vector<double>> {
          // Here we really want like std::nextafter, but instead we
          // just stay in the two contiguous ranges where the values
          // are monotonic. As an improvement, it would be acceptable
          // to go between -0 and 0.
          // Positive is 0-0x7c00. Negative is 0x8000-0xfc00.
          if (u >= 0x0000 && u <= 0x7c00) {
            for (uint16 v = u + dir;
                 v >= 0x0000 && v <= 0x7c00;
                 u += dir) {
              if (!deriv[v].empty())
                return std::make_pair(v, deriv[v]);
            }
            // None found.
            return std::make_pair(u, std::vector<double>{});

          } else if (u >= 0x8000 && u <= 0xfc00) {
            for (uint16 v = u + dir;
                 v >= 0x8000 && v <= 0xfc00;
                 u += dir) {
              if (!deriv[v].empty())
                return std::make_pair(v, deriv[v]);
            }
            // None found.
            return std::make_pair(u, std::vector<double>{});

          } else {
            CHECK(false) << u;
          }
        };

        // The previous y that has a derivative, and the next.
        const auto [pyu, pv] = GetAdjacentBucket(yu, -1);
        const auto [nyu, nv] = GetAdjacentBucket(yu, +1);

        // We should have adjacent points either up or down.
        CHECK(!pv.empty() || nv.empty()) << yu;

        if (!pv.empty() && !nv.empty()) {
          // If we have both, interpolate.
          double py = UnpackFloat(pyu);
          double ny = UnpackFloat(nyu);
          double y = UnpackFloat(yu);

          double pd = AverageVec(pv);
          double nd = AverageVec(nv);

          // These could be in one order or the other depending
          // on whether we were looping over negative or positive
          // floats.
          if (py <= y && y <= ny) {
            double f = (y - py) / ny - py;
            deriv_out[yu] = std::lerp(pd, nd, f);
          } else if (ny <= y && y <= py) {
            double f = (y - ny) / py - ny;
            deriv_out[yu] = std::lerp(nd, pd, f);
          } else {
            CHECK(false) << "y points should be ordered";
          }
        }
      }
    });

  GradUtil::ForEveryFinite16([&deriv_out](uint16 yu) {
      CHECK(std::isfinite(deriv_out[yu])) << yu;
    });

  // TODO: Output derivative!
  ImageRGBA deriv_img(256, 256);

  string out_fwd = "static const uint16_t forward[65536] = {\n";
  for (int i = 0; i < 65536; i++) {
    if (i > 0 && i % 8 == 0) out_fwd += "\n";
    StringAppendF(&out_fwd, "0x%04x,", state.table[i]);
  }
  out_fwd += "\n};\n";
  Util::WriteFile("the-data.h", out_fwd);

  ImageRGBA img(IMAGE_SIZE, IMAGE_SIZE);
  img.Clear32(0x000000FF);
  GradUtil::Grid(&img);
  GradUtil::Graph(state.table, 0xFFFFFF77, &img);

  // Graph derivative within this range.
  double max_mag = 0.0;
  GradUtil::ForPosNeg1([&deriv_out, &max_mag](uint16 u) {
      if (std::isfinite(deriv_out[u]))
        max_mag = std::max(std::abs(deriv_out[u]), max_mag);
    });

  const double scale = ((IMAGE_SIZE / 2.0) * 0.9) / max_mag;
  GradUtil::ForPosNeg1([&img, &deriv_out, scale](uint16 u) {
      // Or plot some error pixels. But we assert this above.
      if (!std::isfinite(deriv_out[u])) return;

      // Scaled
      double y = UnpackFloat(u);
      int ys = (int)std::round((IMAGE_SIZE / 2) + -y * (IMAGE_SIZE / 2.0));

      double d = deriv_out[u] * scale;
      int ds = (int)std::round((IMAGE_SIZE / 2) + d);
      img.BlendPixel32(ds, ys, 0xFFFF0077);
    });

  string md = StringPrintf("%0.5f", max_mag);
  img.BlendText32(IMAGE_SIZE - md.size() * 9 - 6, 2, 0xFFFF0077,
                  md);

  string filename = "state.png";
  img.Save(filename);
  return 0;
}
