
#include <string>
#include <cmath>
#include <memory>
#include <cstdint>
#include <array>
#include <vector>
#include <algorithm>

#include "base/logging.h"
#include "base/stringprintf.h"

#include "util.h"
#include "image.h"
#include "bounds.h"
#include "opt/optimizer.h"
#include "half.h"
#include "array-util.h"

#include "grad-util.h"

using namespace std;

// using half = _Float16;

using uint16 = uint16_t;
using half_float::half;
using namespace half_float::literal;

static constexpr int SAMPLES = 1000;

// Non-finite ranges for half: 7c00-7fff
// fc00-ffff
// So this representation is probably bad
// to search over as integers, since it
// is not monotonic and has two holes in it.

// Permutes u16 such that the space is monotonic when
// interpreted as half, and all values are finite in [0, NUM_FINITE16);
static constexpr uint16_t NUM_FINITE16 = 0xFFFF - 0x800 + 1;
static uint16_t OrderU16(uint16 u) {
  if (u < 0x7C00) {
    // Put negative numbers first.
    return 0xFBFF - u;
  } else if (u < NUM_FINITE16) {
    return u - 0x7C00;
  } else {
    // We don't really care about the order here (maybe we
    // should care about -/+ infinity though?). But make this
    // actually be a permutation of uint16.
    uint16_t v = u - NUM_FINITE16;
    if (v < 0x400) {
      return 0xFC00 + v;
    } else {
      return 0x7C00 + (v - 0x400);
    }
  }
}

template<class fptype_, size_t N_>
struct Func {
  using fptype = fptype_;
  static constexpr int N = N_;
  Func(const std::array<fptype, N> &args) : args(args) {}
  virtual fptype Eval(fptype f) const {
	return f;
  }

  virtual string Exp() const  = 0;
  std::array<fptype, N> args;
};

struct Function1 : public Func<float, 1> {
  using Func::Func;
  float Eval(float f) const override {
    const float scale = args[0];
	float g = f * scale;
	float h = g / scale;
	return h;
  }
  string Exp() const override {
    const float scale = args[0];
	return StringPrintf("(f * %.9g) / %.9g", scale, scale);
  }
};

struct Function2 : public Func<float, 2> {
  using Func::Func;
  float Eval(float f) const override {
    const auto &[scale, off] = args;
	float g = f * scale + off;
	float h = (g - off) / scale;
	return h;
  }
  string Exp() const override {
    const auto &[scale, off] = args;
	return StringPrintf("((f * %.9g + %.9g) - %.9g) / %.9g",
						scale, off, off, scale);
  }
};

struct Function3 : public Func<float, 2> {
  using Func::Func;
  float Eval(float f) const override {
    const auto &[scale, off] = args;
	float g = (f / scale) * off;
	float h = (g / off) * scale;
	return h;
  }
  string Exp() const override {
    const auto &[scale, off] = args;

	return StringPrintf("((f / %.9g) * %.9g) / %.9g) * %.9g",
						scale, off, off, scale);
  }
};

struct Function4 : public Func<half, 2> {
  using Func::Func;
  half Eval(half f) const override {
    const auto &[scale, off] = args;

	half g = (f * off) * scale;
	half h = (g / scale) / off;
	return h;
  }
  string Exp() const override {
    const auto &[scale, off] = args;

	return StringPrintf("((f * %.9g) * %.9g) / %.9g) / %.9g",
						off, scale, scale, off);
  }
};

struct Function5 : public Func<half, 2> {
  using Func::Func;
  half Eval(half f) const override {
    const auto &[scale, off] = args;

	half g = (f * off) * scale;
	half h = (g / scale) / off;
	return h;
  }
  string Exp() const override {
    const auto &[scale, off] = args;
	return StringPrintf("((f + %.9g) * %.9g) / %.9g) - %.9g",
						off, scale, scale, off);
  }
};

struct Function8 : public Func<half, 8> {
  using Func::Func;
  half Eval(half v) const override {
    const auto &[a, b, c, d, e, f, g, h] = args;
	v += a;
	v *= b;
	v /= c;
	v *= d;
	v /= e;
	v *= f;
	v /= g;
	v -= h;
	return v;
  }
  string Exp() const override {
	string ret;
	for (const double z : args) {
	  StringAppendF(&ret, "%.9g, ", z);
	}
	return ret;
  }
};

struct Function16 : public Func<half, 16> {
  using Func::Func;
  half Eval(half v) const override {
    const auto &[a, b, c, d, e, f, g, h,
                 i, j, k, l, m, n, o, p] = args;
    v += 8;
    v += a;
	v *= b;
	v += c;
	v += d;
	v += e;
    v += 16.0;
    v += f;
    v += g;
    v *= h;
    v += i;
    v += j;
    v += 16.0;
    v += k;
    v += l;
    v += m;
    v += n;
	v *= o;
	v -= p;
	return v;
  }
  string Exp() const override {
	string ret;
	for (const double z : args) {
	  StringAppendF(&ret, "%.9g, ", z);
	}
	return ret;
  }
};

// Count the number of distinct values (e.g. in the codomain of the
// function).
template<class fptype>
[[maybe_unused]]
static int DistinctValues(const std::vector<fptype> &samples_in) {
  std::vector<fptype> samples = samples_in;
  std::sort(samples.begin(), samples.end());
  int values = 1;
  for (int i = 0; i < samples.size() - 1; i++) {
    if (samples[i] != samples[i + 1]) values++;
  }
  return values;
}

// N here has to agree with arity of F being optimized.
using GradOptimizer = Optimizer<16, 0, uint8>;

template<class F>
static GradOptimizer::return_type OptimizeMe(GradOptimizer::arg_type arg) {
  static constexpr int N = F::N;
  using fptype = F::fptype;
  static_assert(GradOptimizer::num_ints == N);

  // This could be a type-specific conversion?
  std::array<fptype, N> fargs =
    MapArray([](int u) {
        return GradUtil::GetHalf(OrderU16((uint16)std::clamp(u, 0, 65535)));
      }, arg.first);

  // XXX We could even CHECK this now.
  for (const fptype f : fargs)
    if (!std::isfinite(f))
      return GradOptimizer::INFEASIBLE;

  vector<fptype> samples;
  vector<double> error_samples, deriv_samples;
  F fn(fargs);

  // from -1 to 1.
  auto XAt = [&](int i) -> fptype {
	  return (fptype)(
          (i / (float)(SAMPLES - 1)) * 2.0f - 1.0f);
	};
  auto YAt = [&](int i) {
	  return fn.Eval(XAt(i));
	};
  for (int i = 0; i < SAMPLES; i++) {
	// in [-1, 1]
	fptype in = XAt(i);
	fptype out = YAt(i);
	if (!std::isfinite((float)out)) return GradOptimizer::INFEASIBLE;
	samples.push_back(out);

	double diff = (out - in);
	if (!std::isfinite((float)diff)) return GradOptimizer::INFEASIBLE;
	error_samples.push_back(diff);

	fptype out_prev = YAt(i - 1);
	double deriv = out - out_prev;
	if (!std::isfinite((float)deriv)) return GradOptimizer::INFEASIBLE;
	deriv_samples.push_back(deriv);
  }

  int distinct_values = DistinctValues(samples);
  /*
  if (distinct_values < 25)
    return std::make_pair(100000000.0 - 100.0 * distinct_values,
                          std::nullopt);
  */

  float frac_distinct = distinct_values / (float)SAMPLES;

  // Compare to a linear interpolation of the first and last
  // endpoints.
  double f0 = samples[0];
  double fend = (double)samples[SAMPLES - 1];
  double rise = fend - f0;
  // These solutions are uninteresting even if there is error
  // in between.
  // if (f0 == fend) return GradOptimizer::INFEASIBLE;
  if (rise <= 0.0)
    return std::make_pair(100000000.0 - rise, std::nullopt);
  double error = 0.0;
  for (int i = 0; i < SAMPLES; i++) {
	double frac = i / (double)(SAMPLES - 1);
	double linear = frac * rise;
	double diff = (double)samples[i] - linear;
	error += sqrt(diff * diff);
  }

  error /= SAMPLES;

  double penalty = 0.0;
  // Prefer output range in [-1,1].
  // e.g. if we have -3, then -1 - -3 = -1 + 3 = 2;
  if (f0 < -1.0) penalty += -1.0 - f0;
  else if (f0 > 0) penalty += 1.0 + f0;
  if (fend > 1.0) penalty += fend - 1.0;
  else if (fend < -1.0) penalty += 1.0 - fend;


  // Prefer second derivative to be close to zero.
  double d2 = 0.0;
  for (int i = 1; i < deriv_samples.size(); i++) {
	double d = deriv_samples[i] - deriv_samples[i - 1];
	d2 += fabs(d); // sqrt(d * d);
  }

  d2 /= (deriv_samples.size() - 1);

  double shape_dist = 0.0;
  {
    double vneg = samples[0];
    double v0 = fn.Eval((half)0.0);
    double vone = samples[SAMPLES - 1];

    double dneg = vneg - -0.1;
    double d0 = v0 - 0.0;
    double done = vone - 1.0;
    shape_dist += sqrt(dneg * dneg);
    shape_dist += 2 * sqrt(d0 * d0);
    shape_dist += sqrt(done * done);
  }


  // Want MORE error, so it has negative sign.
  /*
  return make_pair(100.0 * penalty + 3 * d2 - 10 * error -
                   frac_distinct * 10.0, make_optional('*'));
  */
  return make_pair(10 * shape_dist - frac_distinct + d2 - error,
                   make_optional('*'));
}

template<class fptype, size_t N>
[[maybe_unused]]
static void Stats(Func<fptype, N> *fn) {
  vector<fptype> samples;
  vector<double> error_samples, deriv_samples;

  auto XAt = [&](int i) -> fptype {
	  return (fptype)((i / (float)(SAMPLES - 1)) * 2.0f - 1.0f);
	};
  auto YAt = [&](int i) {
	  return fn->Eval(XAt(i));
	};
  for (int i = 0; i < SAMPLES; i++) {
	// in [-1, 1]
	fptype in = XAt(i);
	fptype out = YAt(i);
	if (!std::isfinite((float)out)) {
	  printf("infinite %d", i);
	}
	samples.push_back(out);

	double diff = (out - in);
	if (!std::isfinite((float)diff)) {
	  printf("infinite2 %d", i);
	}
	error_samples.push_back(diff);

	fptype out_prev = YAt(i - 1);
	double deriv = out - out_prev;
	if (!std::isfinite((float)deriv)) {
	  printf("infinite2 %d", i);
	}
	deriv_samples.push_back(deriv);
  }

  const int distinct_values = DistinctValues(samples);

  // Compare to a linear interpolation of the first and last
  // endpoints.
  double f0 = samples[0];
  double fend = (double)samples[SAMPLES - 1];
  double rise = fend - f0;

  double error = 0.0;
  for (int i = 0; i < SAMPLES; i++) {
	double frac = i / (double)(SAMPLES - 1);
	double linear = frac * rise;
	double diff = (double)samples[i] - linear;
	error += sqrt(diff * diff);
  }

  error /= SAMPLES;

  double penalty = 0.0;
  // Prefer output range in [-1,1].
  // e.g. if we have -3, then -1 - -3 = -1 + 3 = 2;
  if (f0 < -1.0) penalty += -1.0 - f0;
  else if (f0 > 0) penalty += 1.0 + f0;
  if (fend > 1.0) penalty += fend - 1.0;
  else if (fend < -1.0) penalty += 1.0 - fend;

  // Prefer second derivative to be close to zero.
  double d2 = 0.0;
  for (int i = 1; i < deriv_samples.size(); i++) {
	double d = deriv_samples[i] - deriv_samples[i - 1];
	d2 += sqrt(d * d);
  }

  d2 /= (deriv_samples.size() - 1);

  double score = penalty + d2 - error;
  printf("f0: %.11g, fend: %.11g\n"
		 "error %.11g, d2 %.11g\n"
         "penalty %.11g\n"
         "distinct values: %d\n"
		 "so score %.11g\n",
		 f0, fend,
		 error, d2,
         penalty,
         distinct_values,
		 score);
}

template<class fptype, size_t N>
[[maybe_unused]]
static void Graph(Func<fptype, N> *fn) {
  Bounds bounds, error_bounds, nonlinear_bounds, deriv_bounds;
  double total_diff = 0.0;
  vector<double> samples, error_samples, nonlinear_samples, deriv_samples;

  auto XAt = [&](int i) -> fptype {
	  return (fptype)((i / (float)(SAMPLES - 1)) * 2.0f - 1.0f);
	};
  auto YAt = [&](int i) {
	  return fn->Eval(XAt(i));
	};
  for (int i = 0; i < SAMPLES; i++) {
	// in [-1, 1]
	fptype in = XAt(i);
	fptype out = YAt(i);
	fptype out_prev = YAt(i - 1);
	double diff = (out - in);
	samples.push_back(out);
	bounds.Bound(i, out);

	error_samples.push_back(diff);
	error_bounds.Bound(i, diff);

	double deriv = out - out_prev;
	deriv_samples.push_back(deriv);
	deriv_bounds.Bound(i, deriv);
  }
  printf("Total diff: %.19g\n", total_diff);

  // Compare to a linear interpolation of the first and last
  // endpoints.
  {
	double f0 = samples[0];
	printf("Linear: %.9g to %.9g\n",
		   (double)f0, (double)samples[SAMPLES - 1]);
	double rise = (double)samples[SAMPLES - 1] - f0;
	double error = 0.0;
	for (int i = 0; i < SAMPLES; i++) {
	  double frac = i / (double)(SAMPLES - 1);
	  double linear = frac * rise;
	  double diff = samples[i] - linear;
	  // printf("%.9g want %.9g\n", samples[i], linear);
	  error += diff * diff;
	  nonlinear_samples.push_back(diff);
	  nonlinear_bounds.Bound(i, diff);
	}
	printf("Squared error vs linear: %.19g\n", error);
  }


  // constexpr int WIDTH = 512 + 200, HEIGHT = 512 + 200;
  constexpr int WIDTH = 1024, HEIGHT = 1024;

  bounds.AddMarginFrac(0.01);
  error_bounds.AddMarginFrac(0.01);
  nonlinear_bounds.AddMarginFrac(0.01);
  deriv_bounds.AddMarginFrac(0.01);

  {
    ImageRGBA img(WIDTH, HEIGHT);
    img.Clear32(0x000000FF);

	Bounds::Scaler error_scaler = error_bounds.Stretch(WIDTH, HEIGHT).FlipY();
    const int yaxis = error_scaler.ScaleY(0);
    img.BlendLine32(0, yaxis, WIDTH - 1, yaxis, 0xFFFFFF3F);

	bool lo = false;
	for (int x = 0; x < WIDTH; x += 10) {
	  img.BlendLine32(x, 0, x, HEIGHT - 1,
					  lo ? 0xFFFFFF11 : 0xFFFFFF22);
	  lo = !lo;
	}

	int ypos = 11;
	auto Plot = [&img, &ypos](const vector<double> &samples,
							  const Bounds &bounds,
							  uint32_t rgb,
							  const std::string &name) {
		Bounds::Scaler scaler =
		  bounds.Stretch(WIDTH, HEIGHT).FlipY();

		double low = 1/0.0, high = -1/0.0;
		for (int i = 0; i < samples.size(); i++) {
		  double d = samples[i];
		  low = std::min(low, d);
		  high = std::max(high, d);
		  int x = round(scaler.ScaleX(i));
		  int y = round(scaler.ScaleY(d));
		  img.BlendPixel32(x, y, rgb | 0xEE);
		}

		img.BlendText32(1, ypos, rgb | 0x77,
						StringPrintf("%s: %.9g to %.9g",
									 name.c_str(), low, high));

		ypos += 10;
	  };

	Plot(error_samples, error_bounds, 0xFF7F7F00, "error");
	Plot(nonlinear_samples, nonlinear_bounds, 0x7F7FFF00, "nonlinear");
	Plot(deriv_samples, deriv_bounds, 0xFFFF7F00, "derivative");
	Plot(samples, bounds, 0x7FFF7F00, "value");

	img.BlendText32(1, 1, 0x888888AA, fn->Exp());

    string filename = "grad.png";
    img.Save(filename);
    printf("Wrote %s\n", filename.c_str());
  }
}


[[maybe_unused]]
void Optimize() {
  // constexpr float LOW = 9.90e37;
  // constexpr float HIGH = 1e38;
  constexpr int32_t LOW = 0;
  constexpr int32_t HIGH = NUM_FINITE16 - 1;

  using F = Function16;
  static const size_t N = F::N;

  std::array<std::pair<int32_t, int32_t>, N> int_bounds;
  for (int i = 0; i < N; i++)
    int_bounds[i] = std::make_pair(LOW, HIGH);

  printf("Search %d to %d\n", LOW, HIGH);

  GradOptimizer optimizer(OptimizeMe<Function16>);
  optimizer.Run(
	  // int bounds
	  int_bounds,
      // float bounds
      {},
	  {}, // calls
	  {}, // feasible calls
	  {20 * 60}, // seconds
	  {});

  auto bo = optimizer.GetBest();
  CHECK(bo.has_value()) << "no feasible??";
  const auto [arg, score, out_] = bo.value();

  // in half-native bit order
  auto u16 =
    MapArray([](int i) -> uint16 {
        return OrderU16(std::clamp(i, 0, 65535));
      },
    arg.first);

  auto fargs =
    MapArray([](int u) { return GradUtil::GetHalf(u); }, u16);

  std::unique_ptr<Func<half, N>> fn(new F(fargs));
  Stats(fn.get());

  printf("Best score: %.17g\n Params:\n", score);

  for (const uint16 u : u16) {
	printf("GradUtil::GetHalf(0x%04x),  // %.17g,\n", u,
           (double)GradUtil::GetHalf(u));
  }

  Graph(fn.get());
}

int main(int argc, char **argv) {
#if 0
  for (int u = 0; u < 65536; u++) {
    uint16_t uu = OrderU16(u);
    half h = GradUtil::GetHalf(uu);
    double v = h;
    const char *isf = std::isfinite(v) ? "" : "NOT";
    printf("%04x -> %04x = %.11g %s\n", u, uu, v, isf);
  }

  return 0;
#endif

  // Optimize();
  //     return 0;

  Optimize();
  return 0;

  if (false) {
	Graph(new Function8(
              {
GradUtil::GetHalf(0xb809),  // -0.50439453125,
GradUtil::GetHalf(0x0560),  // 8.20159912109375e-05,
GradUtil::GetHalf(0x4f40),  // 29,
GradUtil::GetHalf(0xa55e),  // -0.020965576171875,
GradUtil::GetHalf(0x0549),  // 8.0645084381103516e-05,
GradUtil::GetHalf(0x7a76),  // 52928,
GradUtil::GetHalf(0x1291),  // 0.00080156326293945312,
GradUtil::GetHalf(0xf414),  // -16704,
              }
						));
  }

  if (false) {
	// Error_Bounds 0 -5.9604644775e-08 to 999 5.9604644775e-08
	static constexpr float SCALE = 6.0077242583987507e+37;
	static constexpr float OFF = 9.9159268948476343e+37;
	Graph(new Function2({SCALE, OFF}));
  }

  if (false) {
	// static constexpr float SCALE = 9.9260844311214201e+37;
	// static constexpr float OFF = 9.9630854128974192e+37;
	static constexpr float SCALE = 9.9260844311214201e+37f;
	static constexpr float OFF = 9.9630854128974192e+37f;
	Graph(new Function3({SCALE, OFF}));
  }

  if (false) {
	// static constexpr float SCALE = 9.9260844311214201e+37;
	// static constexpr float OFF = 9.9630854128974192e+37;
	static constexpr float SCALE = 9.9684294429838515e+37;
	static constexpr float OFF = 9.9438839619657644e+37;
	Graph(new Function3({SCALE, OFF}));
  }

  if (false) {
	// static constexpr float SCALE = 7.5600048108248608e-05f;
	// static constexpr float OFF = 0.00039428695153609361f;

	static const half SCALE = (half)0.4388188340760063f;
	static const half OFF = (half)38235.825656460482f;

	// static constexpr half SCALE = 20765.713900227656f;
	// static constexpr half OFF = 30555.616399484014f;
	Graph(new Function4({SCALE, OFF}));
  }

  return 0;
}

/*
GradUtil::GetHalf(0xa0a0),  // -0.009033203125,
GradUtil::GetHalf(0xa038),  // -0.00823974609375,
GradUtil::GetHalf(0xa037),  // -0.00823211669921875,
GradUtil::GetHalf(0xa03e),  // -0.0082855224609375,
GradUtil::GetHalf(0xa03f),  // -0.00829315185546875,
GradUtil::GetHalf(0xa03f),  // -0.00829315185546875,
GradUtil::GetHalf(0xa03e),  // -0.0082855224609375,
GradUtil::GetHalf(0x9f80),  // -0.00732421875,
*/
