
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
// #include "array-util.h"

using namespace std;

// using half = _Float16;

using uint32 = uint32_t;
using uint16 = uint16_t;
using half_float::half;
using namespace half_float::literal;

static constexpr ColorUtil::Gradient GREEN_BLUE {
  GradRGB(0.0f,  0x00FF00),
  GradRGB(1.0f,  0x0000FF),
};

// Non-finite ranges for half: 7c00-7fff
// fc00-ffff
// So this representation is probably bad
// to search over as integers, since it
// is not monotonic and has two holes in it.
[[maybe_unused]]
static inline half GetHalf(uint16 u) {
  half h;
  static_assert(sizeof (h) == sizeof (u));
  memcpy((void*)&h, (void*)&u, sizeof (u));
  return h;
}

[[maybe_unused]]
static inline uint16 GetU16(half h) {
  uint16 u;
  static_assert(sizeof (h) == sizeof (u));
  memcpy((void*)&u, (void*)&h, sizeof (u));
  return u;
}

// Range of all u16s in [-1, +1].
static constexpr uint16 POS_LOW  = 0x0000; // +0
static constexpr uint16 POS_HIGH = 0x3C00; // +1
static constexpr uint16 NEG_LOW  = 0x8000; // -0
static constexpr uint16 NEG_HIGH = 0xBC00; // -1

// The function (uint16 -> uint16) can be completely described as a table
// of the resulting value for the 65536 inputs.
using Table = std::array<uint16_t, 65536>;

static constexpr int SIZE = 2048;
static void Graph(const Table &table, uint32 color, ImageRGBA *img) {
  CHECK(img->Width() == SIZE);
  CHECK(img->Height() == SIZE);  

  // Loop over [-1, 1].
  auto Plot = [&](uint16 input) {
	  uint16 output = table[input];
	  double x = GetHalf(input);
	  double y = GetHalf(output);

	  int xs = (int)std::round((SIZE / 2) + x * (SIZE / 2));
	  int ys = (int)std::round((SIZE / 2) + -y * (SIZE / 2));

	  ys = std::clamp(ys, 0, SIZE - 1);
	  
	  img->BlendPixel32(xs, ys, color);
	};

  for (int i = NEG_LOW; i < NEG_HIGH; i++) Plot(i);
  for (int i = POS_LOW; i < POS_HIGH; i++) Plot(i);
}

[[maybe_unused]]
void UpdateTable(Table *table, const std::function<half(half)> &f) {
  for (uint16 &u : *table) {
	half in = GetHalf(u);
	half out = f(in);
	u = GetU16(out);
  }
}

[[maybe_unused]]
void UpdateTable16(Table *table, const std::function<uint16(uint16)> &f) {
  for (int i = 0; i < table->size(); i++) {
    // printf("%d\n", i);
    (*table)[i] = f((*table)[i]);
  }
}

Table IdentityTable() {
  Table table;
  for (int i = 0; i < 65536; i++) {
	table[i] = i;
  }
  return table;
}

[[maybe_unused]]
static void GraphRandomIterations() {
  ArcFour rc(StringPrintf("iter %lld", time(nullptr)));
  ImageRGBA img(SIZE, SIZE);
  img.Clear32(0x000000FF);

  Table table = IdentityTable();
  int ITERS = 100;

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

  Graph(table, 0xFFFFFF11, &img);
  
  CHECK(values.size() == ITERS);
  for (int i = 0; i < ITERS; i++) {
	UpdateTable(&table, F(i));
	float f = i / (float)(ITERS - 1);
	uint32 color = ColorUtil::LinearGradient32(GREEN_BLUE, f) & 0xFFFFFF22;
	Graph(table, color, &img);
  }

  Graph(table, 0xFF000044, &img);

  string filename = "iter.png";
  img.Save(filename);
  printf("Wrote %s\n", filename.c_str());
}

// Iteratively try to create a nonlinear shape.
// At each step, we have the table of values, which represents
// the function so far; we also keep the series of steps so we
// can recreate it.

struct Step {
  // Otherwise, add.
  bool mult = false;
  uint16 value = 0;
};

struct State {
  State() : table(IdentityTable()) {}
  Table table;
  std::vector<Step> steps;
};

string StepString(const Step &step) {
  return StringPrintf("%c %.9g (%04x)",
                      step.mult ? '*' : '+',
                      (float)GetHalf(step.value), step.value);
}

// We don't require intermediate states to be centered, because
// we can easily do this after the fact.
// Get the additive and multiplicative offsets. ((f(x) + a) * m).
static std::pair<half, half> Recentering(const Table &table) {
  // first, place f(0) at 0.
  const half offset = -GetHalf(table[GetU16((half)0.0)]);
  const half scale = (half)1.0 /
    (GetHalf(table[GetU16((half)1.0)]) + offset);
  return make_pair(offset, scale);
}

static float Dist(const Table &table) {
  const auto &[offset, scale] = Recentering(table);

  auto RF = [&](half h) -> half {
      return (GetHalf(table[GetU16(h)]) + offset) * scale;
    };
  
  float d0 = RF((half)0.0) - (half)0.0;
  float d1 = RF((half)1.0) - (half)1.0;
  float dn = RF((half)-1.0) - (half)-0.125;

  // In principle this is just sqrt(dn * dn) because of the recentering
  // we just did. It might even be exact (the offset should be, but
  // probably not every number has a reciprocal).
  float dist = sqrtf(d0 * d0) + sqrtf(d1 * d1) + sqrtf(dn * dn);

  return dist;
}

static void ApplyStep(Step step, Table *table) {
  if (step.mult) {
    UpdateTable16(table, [step](uint16 u) {
        return GetU16(GetHalf(u) * GetHalf(step.value));
      });
  } else {
    UpdateTable16(table, [step](uint16 u) {
        return GetU16(GetHalf(u) + GetHalf(step.value));
      });
  }
}

// Add a step to the function, updating the table.
static void Iterate(State *state) {
  float best_dist = 9999999999.0f;
  Step best_step;

  auto Try = [state, &best_dist, &best_step](Step step) {
      Table new_table = state->table;
      ApplyStep(step, &new_table);
      float dist = Dist(new_table);
      if (dist < best_dist) {
        best_step = step;
        dist = best_dist;
      }
    };

  for (bool mult : {false, true}) {
    for (int u = POS_LOW; u < POS_HIGH; u++)
      Try(Step({.mult = mult, .value = (uint16)u}));
    for (int u = NEG_LOW; u < NEG_HIGH; u++)
      Try(Step({.mult = mult, .value = (uint16)u}));
  }

  ApplyStep(best_step, &state->table);
  state->steps.push_back(best_step);
}

static void MakeIterated() {
  State state;
  for (int i = 0; i < 100; i++) {
    Iterate(&state);
    string ss = StepString(state.steps.back());
    printf("%d/100 %s\n", i, ss.c_str());
    
    {
      ImageRGBA img(SIZE, SIZE);
      img.Clear32(0x000000FF);
      Table new_table = state.table;

      const auto &[offset, scale] = Recentering(new_table);
      ApplyStep(Step{.mult = false, .value = GetU16(offset)}, &new_table);
      ApplyStep(Step{.mult = true, .value = GetU16(scale)}, &new_table);
      
      Graph(new_table, 0xFFFFFF77, &img);
      img.BlendText32(3, 3, 0xFFFF55AA, ss);
      string filename = StringPrintf("iterate-%03d.png", i);
      img.Save(filename);
    }
  }

  ImageRGBA img(SIZE, SIZE);
  Graph(state.table, 0xFF000077, &img);
  
  string filename = "iterate.png";
  img.Save(filename);
  printf("Wrote %s\n", filename.c_str());
}
  
int main(int argc, char **argv) {

  State state;
  for (int i = 0; i < 400; i++) {
    ApplyStep(Step{.mult = true, .value = 0xbbffu}, &state.table);
  }
  
  ImageRGBA img(SIZE, SIZE);
  img.Clear32(0x000000FF);
  Table new_table = state.table;

  const auto &[offset, scale] = Recentering(new_table);
  printf("Offset %.9g Scale %.9g\n", (float)offset, (float)scale);
  ApplyStep(Step{.mult = false, .value = GetU16(offset)}, &new_table);
  ApplyStep(Step{.mult = true, .value = GetU16(scale)}, &new_table);
      
  Graph(new_table, 0xFFFFFF77, &img);
  string filename = "iterated.png";
  img.Save(filename);

  return 0;
}
