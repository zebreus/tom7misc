
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
  ImageRGBA img(IMAGE_SIZE, IMAGE_SIZE);
  img.Clear32(0x000000FF);
  State state = MakeTable1();

  GradUtil::Grid(&img);
  GradUtil::Graph(state.table, 0xFFFFFF77, &img);

  // TODO: Output it, and derivative!

  string filename = "state.png";
  img.Save(filename);
  return 0;
}
