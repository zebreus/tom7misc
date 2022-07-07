
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

[[maybe_unused]]
static void GraphRandomIterations() {
  ArcFour rc(StringPrintf("iter %lld", time(nullptr)));
  ImageRGBA img(IMAGE_SIZE, IMAGE_SIZE);
  img.Clear32(0x000000FF);

  Table table = GradUtil::IdentityTable();
  const int ITERS = 100;

  // Small values that sum to 0.0.
  std::vector<double> values;
  CHECK(ITERS % 4 == 0);
  for (int i = 0; i < ITERS / 4; i++) {
    values.push_back((i + 1));
    values.push_back(-(i + 1));
    values.push_back(1.0 / (i + 1));
    values.push_back(-1.0 / (i + 1));
  }

  Shuffle(&rc, &values);

  int sum = 0;
  for (int i : values) sum += i;
  CHECK(sum == 0);

  auto F = [&](int i) {
      return std::function<half(half)>([&values, i](half h) -> half {
          CHECK(i < values.size()) << i << " vs " << values.size();
          return h + (half)(values[i]);
        });
    };

  GradUtil::Graph(table, 0xFFFFFF11, &img);

  CHECK(values.size() == ITERS);
  for (int i = 0; i < ITERS; i++) {
    GradUtil::UpdateTable(&table, F(i));
    float f = i / (float)(ITERS - 1);
    uint32 color = ColorUtil::LinearGradient32(
        GradUtil::GREEN_BLUE, f) & 0xFFFFFF22;
    GradUtil::Graph(table, color, &img);
  }

  GradUtil::Graph(table, 0xFF000044, &img);

  string filename = "iter.png";
  img.Save(filename);
  printf("Wrote %s\n", filename.c_str());
}

// Iteratively try to create a nonlinear shape.
// At each step, we have the table of values, which represents
// the function so far; we also keep the series of steps so we
// can recreate it.

// Add a step to the function, updating the table.
static void Iterate(State *state) {
  float best_dist = 9999999999.0f;
  Step best_step;

  auto Try = [state, &best_dist, &best_step](Step step) {
      Table new_table = state->table;
      GradUtil::ApplyStep(step, &new_table);
      float dist = GradUtil::Dist(new_table);
      if (dist < best_dist) {
        best_step = step;
        dist = best_dist;
      }
    };

  for (bool mult : {false, true}) {
    GradUtil::ForEveryFinite16([&](uint16 u) {
        Try(Step({.mult = mult, .value = u}));
      });
  }

  GradUtil::ApplyStep(best_step, &state->table);
  state->steps.push_back(best_step);
}

static void MakeIterated() {
  State state;
  for (int i = 0; i < 100; i++) {
    Iterate(&state);
    string ss = GradUtil::StepString(state.steps.back());
    printf("%d/100 %s\n", i, ss.c_str());

    {
      ImageRGBA img(IMAGE_SIZE, IMAGE_SIZE);
      img.Clear32(0x000000FF);
      Table new_table = state.table;

      const auto &[offset, scale] = GradUtil::Recentering(new_table);
      GradUtil::ApplyStep(Step{.mult = false,
                               .value = GradUtil::GetU16(offset)},
        &new_table);
      GradUtil::ApplyStep(Step{.mult = true,
                               .value = GradUtil::GetU16(scale)},
        &new_table);

      GradUtil::Graph(new_table, 0xFFFFFF77, &img);
      img.BlendText32(3, 3, 0xFFFF55AA, ss);
      string filename = StringPrintf("iterate-%03d.png", i);
      img.Save(filename);
    }
  }

  ImageRGBA img(IMAGE_SIZE, IMAGE_SIZE);
  GradUtil::Graph(state.table, 0xFF000077, &img);

  string filename = "iterate.png";
  img.Save(filename);
  printf("Wrote %s\n", filename.c_str());
}

// Good: Iterate * 0x3bff 12000 times, recenter.

int main(int argc, char **argv) {

  Table table_f1 = GradUtil::MakeTable1().table;

  Asynchronously asyn(8);
  State state;

  // First value <1
  // const uint16 c = 0x3bffu;
  // it may be a little faster to do 3bfe?
  // This also works: first value >1
  const uint16 c = 0x3c01u;
  // const uint16 c = 0x3bfau;

  GradUtil::ApplyStep(Step{.mult = false,
                           .value = GradUtil::GetU16(0.67_h)},
                      &state.table);

  for (int i = 0; i < 1000; i++) {
    GradUtil::ApplyStep(Step{.mult = true, .value = c}, &state.table);
    /*
      GradUtil::ApplyStep(Step{.mult = false, .value = 0x8020u}, &state.table);
      GradUtil::ApplyStep(Step{.mult = false, .value = 0x0020u}, &state.table);
    */

    if (i % 50 == 0) {
      asyn.Run([&table_f1, new_table = state.table, i]() mutable {
          ImageRGBA img(IMAGE_SIZE, IMAGE_SIZE);
          img.Clear32(0x000000FF);
          GradUtil::Grid(&img);
          // Table new_table = state.table;

          const auto &[offset, scale] = GradUtil::Recentering(new_table);
          printf("Iter %d. Offset %04x = %.9g Scale %04x = %.9g\n",
                 i,
                 GradUtil::GetU16(offset), (float)offset,
                 GradUtil::GetU16(scale), (float)scale);
          GradUtil::ApplyStep(Step{.mult = false,
                                   .value = GradUtil::GetU16(offset)},
            &new_table);
          GradUtil::ApplyStep(Step{.mult = true,
                                   .value = GradUtil::GetU16(scale)},
            &new_table);

          Table graph_table = GradUtil::Minus(table_f1, new_table);

          GradUtil::ApplyStep(Step{.mult = true,
                                   .value = GradUtil::GetU16(10.0_h)},
            &graph_table);

          GradUtil::Graph(graph_table, 0xFFFFFF77, &img);
          string filename = StringPrintf("iterated-%05d.png", i);
          img.Save(filename);
        });
    }
  }

#if 0
  ImageRGBA img(IMAGE_SIZE, IMAGE_SIZE);
  img.Clear32(0x000000FF);
  Table new_table = state.table;

  const auto &[offset, scale] = GradUtil::Recentering(new_table);
  printf("Offset %.9g Scale %.9g\n", (float)offset, (float)scale);
  GradUtil::ApplyStep(Step{.mult = false,
                           .value = GradUtil::GetU16(offset)}, &new_table);
  GradUtil::ApplyStep(Step{.mult = true,
                           .value = GradUtil::GetU16(scale)}, &new_table);

  GradUtil::Graph(new_table, 0xFFFFFF77, &img);
  string filename = "iterated.png";
  img.Save(filename);
#endif

  return 0;
}
