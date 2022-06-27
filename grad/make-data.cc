
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

static constexpr int IMAGE_SIZE = 256; // 16384; // 1920;

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
  ImageRGBA deriv(256, 256);

  string out_fwd = "static const uint16_t forward[65536] = {\n";
  for (int i = 0; i < 65536; i++) {
    if (i > 0 && i % 8 == 0) out_fwd += "\n";
    StringAppendF(&out_fwd, "0x%04x,", state.table[i]);
  }
  out_fwd += "\n};\n";
  Util::WriteFile("the-data.h", out_fwd);

  // TODO: Output it, and derivative!

  ImageRGBA img(IMAGE_SIZE, IMAGE_SIZE);
  img.Clear32(0x000000FF);
  GradUtil::Grid(&img);
  GradUtil::Graph(state.table, 0xFFFFFF77, &img);
  string filename = "state.png";
  img.Save(filename);
  return 0;
}
