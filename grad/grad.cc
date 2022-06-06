
#include <string>
#include <cmath>

#include "base/logging.h"
#include "base/stringprintf.h"

#include "util.h"
#include "image.h"
#include "bounds.h"
#include "opt/optimizer.h"

using namespace std;

using half = _Float16;

static constexpr int SAMPLES = 1000;

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
	  {240}, // seconds
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

using GradOptimizer = Optimizer<0, 8, uint8>;

static GradOptimizer::return_type OptimizeMe(GradOptimizer::arg_type arg) {
  using fptype = Function8::fptype;
  auto [a, b, c, d, e, f, g, h] = arg.second;
  vector<fptype> samples, error_samples, deriv_samples;
  Function8 fn(a, b, c, d, e, f, g, h);

  auto XAt = [&](int i) {
	  return (i / (fptype)(SAMPLES - 1)) * 2.0f - 1.0f;
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
  if (fend > 1.0) penalty += fend - 1.0;
  
  // Prefer second derivative to be close to zero.
  double d2 = 0.0;
  for (int i = 1; i < deriv_samples.size(); i++) {
	double d = deriv_samples[i] - deriv_samples[i - 1];
	d2 += sqrt(d * d);
  }

  d2 /= (deriv_samples.size() - 1);
  
  // Want MORE error, so it has negative sign.
  return make_pair(10.0 * penalty + d2 - error, make_optional('*'));
}

static void Stats(half a, half b, half c, half d,
				  half e, half f, half g, half h) {
  using fptype = Function8::fptype;
  vector<fptype> samples, error_samples, deriv_samples;
  Function8 fn(a, b, c, d, e, f, g, h);

  auto XAt = [&](int i) {
	  return (i / (fptype)(SAMPLES - 1)) * 2.0f - 1.0f;
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
  if (fend > 1.0) penalty += fend - 1.0;
  
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
		 "so score %.11g\n",
		 f0, fend,
		 error, d2,
		 score);
}


[[maybe_unused]]
static std::array<double, 8> Optimize() {
  // constexpr float LOW = 9.90e37;
  // constexpr float HIGH = 1e38;
  constexpr float LOW = 0;
  constexpr float HIGH = 65504;
  
  printf("Search %.11g to %.11g\n", LOW, HIGH);
  
  GradOptimizer optimizer(OptimizeMe);
  optimizer.Run(
	  // int bounds
	  {},
	  // scale bounds
	  {make_pair(LOW, HIGH),
	   make_pair(LOW, HIGH),
	   make_pair(LOW, HIGH),
	   make_pair(LOW, HIGH),
	   make_pair(LOW, HIGH),
	   make_pair(LOW, HIGH),
	   make_pair(LOW, HIGH),
	   make_pair(LOW, HIGH),},
	  {}, // calls
	  {}, // feasible calls
	  {30}, // seconds
	  {});

  auto bo = optimizer.GetBest();
  CHECK(bo.has_value()) << "no feasible??";
  const auto [arg, score, out_] = bo.value();
  {
	auto [a, b, c, d, e, f, g, h] = arg.second;
	Stats(a, b, c, d, e, f, g, h);
  }
  printf("Best score: %.17g\n Params:\n", score);
  for (const double d : arg.second) {
	printf("%.17g,\n", d);
  }
  return arg.second;
}


template<class fptype>
[[maybe_unused]]
static void Graph(Func<fptype> *fn) {
  Bounds bounds, error_bounds, nonlinear_bounds, deriv_bounds;
  double total_diff = 0.0;
  vector<double> samples, error_samples, nonlinear_samples, deriv_samples;

  auto XAt = [&](int i) {
	  return (i / (fptype)(SAMPLES - 1)) * 2.0f - 1.0f;
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

  
  constexpr int WIDTH = 512 + 200, HEIGHT = 512 + 200;

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
  }
}

int main(int argc, char **argv) {

  //Optimize();
   //   return 0;
   
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

	static constexpr float SCALE = 0.4388188340760063f;
	static constexpr float OFF = 38235.825656460482f;
	
	// static constexpr half SCALE = 20765.713900227656f;
	// static constexpr half OFF = 30555.616399484014f;
	Graph(new Function4(SCALE, OFF));
  }

  if (true) {
	Graph(new Function8(
217.08133671536692,
68.328132197676823,
70.589646206697722,
195.43627373883018,
68.168176480755761,
70.357486727210997,
196.29278662281129,
216.25857880045993
						));
}
  
  return 0;
}
