
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

static constexpr int SIZE = 1024;
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
void UpdateTable(Table *table, std::function<half(half)> f) {
  for (uint16 &u : *table) {
	half in = GetHalf(u);
	half out = f(in);
	u = GetU16(out);
  }
}

Table IdentityTable() {
  Table table;
  for (int i = 0; i < 65536; i++) {
	table[i] = i;
  }
  return table;
}

int main(int argc, char **argv) {
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
  
  /*
	[i](half h) -> half {
		h /= (half)((i + 1) * 2.0);
		h += (half)(i * 3.0 / 2);
		h -= (half)(i * 3.0 / 2);
		h *= (half)((i + 1) * 2.0);
		return h;
	  }
  */
  
  string filename = "iter.png";
  img.Save(filename);
  printf("Wrote %s\n", filename.c_str());
}

#if 0
int main(int argc, char **argv) {
  std::map<uint16_t, double> gamut;
  for (int u = 0; u < 65536; u++) {
    half h = GetHalf(u);
    double v = h;
	if (std::isfinite(v) && v >= -1.0 && v <= 1.0) {
	  gamut[u] = v;
	}
  }

  for (const auto &[u, v] : gamut) {
	printf("%04x = %.11g\n", u, v);
  }
  
  return 0;
}
#endif
