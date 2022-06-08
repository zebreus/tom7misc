
#include <string>
#include <cmath>

#include "base/logging.h"
#include "base/stringprintf.h"

#include "util.h"
#include "image.h"
#include "bounds.h"
#include "opt/optimizer.h"
#include "half.h"
#include "array-util.h"

using namespace std;

// using half = _Float16;

using uint16 = uint16_t;
using half_float::half;
using namespace half_float::literal;

static constexpr int SAMPLES = 1000;

static half GetHalf(uint16 u) {
  half h;
  static_assert(sizeof (h) == sizeof (u));
  memcpy((void*)&h, (void*)&u, sizeof (u));
  return h;
}

template<class fptype_>
struct Func {
  using fptype = fptype_;
  virtual fptype Eval(fptype f) const {
	return f;
  }
  
  virtual string Exp() const  = 0;
};

struct Function1 : public Func<float> {
  const float scale;
  Function1(float scale) : scale(scale) {}
  float Eval(float f) const override {
	float g = f * scale;
	float h = g / scale;
	return h;
  }
  string Exp() const override {
	return StringPrintf("(f * %.9g) / %.9g", scale, scale);
  }
};

struct Function2 : public Func<float> {
  const float scale, off;
  Function2(float scale, float off) : scale(scale), off(off) {}
  float Eval(float f) const override {
	float g = f * scale + off;
	float h = (g - off) / scale;
	return h;
  }
  string Exp() const override {
	return StringPrintf("((f * %.9g + %.9g) - %.9g) / %.9g",
						scale, off, off, scale);
  }
};

struct Function3 : public Func<float> {
  const float scale, off;
  Function3(float scale, float off) : scale(scale), off(off) {}
  float Eval(float f) const override {
	float g = (f / scale) * off;
	float h = (g / off) * scale;
	return h;
  }
  string Exp() const override {
	return StringPrintf("((f / %.9g) * %.9g) / %.9g) * %.9g",
						scale, off, off, scale);
  }
};

struct Function4 : public Func<half> {
  const half scale, off;
  Function4(half scale, half off) : scale(scale), off(off) {}
  half Eval(half f) const override {
	half g = (f * off) * scale;
	half h = (g / scale) / off;
	return h;
  }
  string Exp() const override {
	return StringPrintf("((f * %.9g) * %.9g) / %.9g) / %.9g",
						off, scale, scale, off);
  }
};

struct Function5 : public Func<half> {
  const half scale, off;
  Function5(half scale, half off) : scale(scale), off(off) {}
  half Eval(half f) const override {
	half g = (f * off) * scale;
	half h = (g / scale) / off;
	return h;
  }
  string Exp() const override {
	return StringPrintf("((f + %.9g) * %.9g) / %.9g) - %.9g",
						off, scale, scale, off);
  }
};

struct Function8 : public Func<half> {
  half a, b, c, d, e, f, g, h;
  Function8(half a, half b, half c, half d,
			half e, half f, half g, half h) : a(a), b(b), c(c), d(d),
											  e(e), f(f), g(g), h(h) {}
  half Eval(half v) const override {
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
	for (const double z : {a, b, c, d, e, f, g, h}) {
	  StringAppendF(&ret, "%.9g, ", z);
	}
	return ret;
  }
};

#if 0
using GradOptimizer = Optimizer<0, 2, double>;

template<class fptype>
static GradOptimizer::return_type OptimizeMe(GradOptimizer::arg_type arg) {
  auto [scale, off] = arg.second;
  vector<fptype> samples;
  Function5 fn(scale, off);
  for (int i = 0; i < SAMPLES; i++) {
	// in [-1, 1]
	fptype in = (i / (fptype)(SAMPLES - 1)) * 2.0f - 1.0f;
	fptype out = fn.Eval(in);
	if (!std::isfinite((float)out)) return GradOptimizer::INFEASIBLE;
	samples.push_back(out);
  }

  // Compare to a linear interpolation of the first and last
  // endpoints.
  double f0 = samples[0];
  double rise = (double)samples[SAMPLES - 1] - f0;
  double error = 0.0;
  for (int i = 0; i < SAMPLES; i++) {
	double frac = i / (double)(SAMPLES - 1);
	double linear = frac * rise;
	double diff = (double)samples[i] - linear;
	error += diff * diff;
  }

  // No need to normalize since we are comparing by rank.  
  // error /= SAMPLES;

  // Want MORE error.
  return make_pair(-error, make_optional(error));
}

[[maybe_unused]]
static void Optimize() {
  // constexpr float LOW = 9.90e37;
  // constexpr float HIGH = 1e38;
  constexpr float LOW = 0;
  constexpr float HIGH = 65504;
  
  printf("Search %.11g to %.11g\n", LOW, HIGH);
  
  GradOptimizer optimizer(OptimizeMe<half>);
  optimizer.Run(
	  // int bounds
	  {},
	  // scale bounds
	  {make_pair(LOW, HIGH),
	   make_pair(LOW, HIGH)},
	  {}, // calls
	  {}, // feasible calls
	  {60 * 5}, // seconds
	  {});

  auto bo = optimizer.GetBest();
  CHECK(bo.has_value()) << "no feasible??";
  const auto [arg, score, out_] = bo.value();
  const auto [scale, off] = arg.second;
  printf("static constexpr float SCALE = %.17gf;\n"
		 "static constexpr float OFF = %.17gf;\n"
		 "score %.17g\n", scale, off, score);
}
#endif

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

using GradOptimizer = Optimizer<8, 0, uint8>;

static GradOptimizer::return_type OptimizeMe(GradOptimizer::arg_type arg) {
  using fptype = Function8::fptype;
  auto [a, b, c, d, e, f, g, h] =
    MapArray([](int u) {
        return GetHalf((uint16)std::clamp(u, 0, 65535));
      }, arg.first);

  if (!std::isfinite(a) ||
      !std::isfinite(b) ||
      !std::isfinite(c) ||
      !std::isfinite(d) ||
      !std::isfinite(e) ||
      !std::isfinite(f) ||
      !std::isfinite(g) ||
      !std::isfinite(h)) return GradOptimizer::INFEASIBLE;
  
  vector<fptype> samples;
  vector<double> error_samples, deriv_samples;
  Function8 fn((fptype)a, (fptype)b, (fptype)c, (fptype)d,
               (fptype)e, (fptype)f, (fptype)g, (fptype)h);

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
  if (distinct_values < sqrt(SAMPLES))
    return std::make_pair(100000000.0 - 100.0 * distinct_values,
                          std::nullopt);
  
  // Compare to a linear interpolation of the first and last
  // endpoints.
  double f0 = samples[0];
  double fend = (double)samples[SAMPLES - 1];
  // These solutions are uninteresting even if there is error
  // in between.
  if (f0 == fend) return GradOptimizer::INFEASIBLE;
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
  
  // Want MORE error, so it has negative sign.
  return make_pair(10.0 * penalty + 3 * d2 - 10 * error -
                   distinct_values * 0.10, make_optional('*'));
}

static void Stats(half a, half b, half c, half d,
				  half e, half f, half g, half h) {
  using fptype = Function8::fptype;
  vector<fptype> samples;
  vector<double> error_samples, deriv_samples;
  Function8 fn(a, b, c, d, e, f, g, h);

  auto XAt = [&](int i) -> fptype {
	  return (fptype)((i / (float)(SAMPLES - 1)) * 2.0f - 1.0f);
	};
  auto YAt = [&](int i) {
	  return fn.Eval(XAt(i));
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


[[maybe_unused]]
static std::array<uint16_t, 8> Optimize() {
  // constexpr float LOW = 9.90e37;
  // constexpr float HIGH = 1e38;
  constexpr float LOW = 0;
  constexpr float HIGH = 65535;
  
  printf("Search %.11g to %.11g\n", LOW, HIGH);
  
  GradOptimizer optimizer(OptimizeMe);
  optimizer.Run(
	  // int bounds
	  {make_pair(LOW, HIGH),
	   make_pair(LOW, HIGH),
	   make_pair(LOW, HIGH),
	   make_pair(LOW, HIGH),
	   make_pair(LOW, HIGH),
	   make_pair(LOW, HIGH),
	   make_pair(LOW, HIGH),
	   make_pair(LOW, HIGH),},
      // float bounds
      {},
	  {}, // calls
	  {}, // feasible calls
	  {30}, // seconds
	  {});

  auto bo = optimizer.GetBest();
  CHECK(bo.has_value()) << "no feasible??";
  const auto [arg, score, out_] = bo.value();
  {
	auto [a, b, c, d, e, f, g, h] =
      MapArray([](int u) {
          return GetHalf((uint16)std::clamp(u, 0, 65535));
        }, arg.first);
    
	Stats((half)a, (half)b, (half)c, (half)d,
          (half)e, (half)f, (half)g, (half)h);
  }
  printf("Best score: %.17g\n Params:\n", score);

  auto u16 =
    MapArray([](int i) -> uint16 { return std::clamp(i, 0, 65535); },
             arg.first);

  for (const uint16 u : u16) {
	printf("GetHalf(0x%04x),  // %.17g,\n", u, (double)GetHalf(u));
  }
  return u16;
}


template<class fptype>
[[maybe_unused]]
static void Graph(Func<fptype> *fn) {
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

int main(int argc, char **argv) {

  // Optimize();
  //     return 0;

  std::array<uint16_t, 8> uu = Optimize();
  {
    auto [a, b, c, d, e, f, g, h] =
      MapArray(GetHalf, uu);
    Graph(new Function8(a, b, c, d,
                        e, f, g, h));
  }

  return 0;
  
  if (false) {
	Graph(new Function8(
GetHalf(0xb809),  // -0.50439453125,
GetHalf(0x0560),  // 8.20159912109375e-05,
GetHalf(0x4f40),  // 29,
GetHalf(0xa55e),  // -0.020965576171875,
GetHalf(0x0549),  // 8.0645084381103516e-05,
GetHalf(0x7a76),  // 52928,
GetHalf(0x1291),  // 0.00080156326293945312,
GetHalf(0xf414)  // -16704,
						));
  }
  
  if (false) {
	// Error_Bounds 0 -5.9604644775e-08 to 999 5.9604644775e-08
	static constexpr float SCALE = 6.0077242583987507e+37;
	static constexpr float OFF = 9.9159268948476343e+37;
	Graph(new Function2(SCALE, OFF));
  }

  if (false) {
	// static constexpr float SCALE = 9.9260844311214201e+37;
	// static constexpr float OFF = 9.9630854128974192e+37;
	static constexpr float SCALE = 9.9260844311214201e+37f;
	static constexpr float OFF = 9.9630854128974192e+37f;
	Graph(new Function3(SCALE, OFF));
  }

  if (false) {
	// static constexpr float SCALE = 9.9260844311214201e+37;
	// static constexpr float OFF = 9.9630854128974192e+37;
	static constexpr float SCALE = 9.9684294429838515e+37;
	static constexpr float OFF = 9.9438839619657644e+37;
	Graph(new Function3(SCALE, OFF));
  }

  if (false) {
	// static constexpr float SCALE = 7.5600048108248608e-05f;
	// static constexpr float OFF = 0.00039428695153609361f;

	static const half SCALE = (half)0.4388188340760063f;
	static const half OFF = (half)38235.825656460482f;
	
	// static constexpr half SCALE = 20765.713900227656f;
	// static constexpr half OFF = 30555.616399484014f;
	Graph(new Function4(SCALE, OFF));
  }
  
  return 0;
}

/*
GetHalf(0xa0a0),  // -0.009033203125,
GetHalf(0xa038),  // -0.00823974609375,
GetHalf(0xa037),  // -0.00823211669921875,
GetHalf(0xa03e),  // -0.0082855224609375,
GetHalf(0xa03f),  // -0.00829315185546875,
GetHalf(0xa03f),  // -0.00829315185546875,
GetHalf(0xa03e),  // -0.0082855224609375,
GetHalf(0x9f80),  // -0.00732421875,
*/
