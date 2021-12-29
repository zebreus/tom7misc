
#include <string>
#include <cmath>

#include "base/logging.h"
#include "base/stringprintf.h"

#include "util.h"
#include "image.h"
#include "bounds.h"
#include "opt/optimizer.h"

using namespace std;

static constexpr int SAMPLES = 1000;

static float Function(float scale, float f) {
  float g = f * scale;
  float h = g / scale;
  return h;
}

static float Function2(float off, float scale, float f) {
  float g = f * scale + off;
  float h = (g - off) / scale;
  return h;
}

static float Function3(float off, float scale, float f) {
  float g = (f / scale) * off;
  float h = (g / off) * scale;
  return h;
}

using GradOptimizer = Optimizer<0, 2, double>;

static GradOptimizer::return_type OptimizeMe(GradOptimizer::arg_type arg) {
  auto [scale, off] = arg.second;
  vector<double> samples;
  for (int i = 0; i < SAMPLES; i++) {
	// in [-1, 1]
	float in = (i / (float)(SAMPLES - 1)) * 2.0f - 1.0f;
	float out = Function3(off, scale, in);
	if (!std::isfinite(out)) return GradOptimizer::INFEASIBLE;
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
	double diff = samples[i] - linear;
	error += diff * diff;
  }

  // No need to normalize since we are comparing by rank.  
  // error /= SAMPLES;

  // Want MORE error.
  return make_pair(-error, make_optional(error));
}

[[maybe_unused]]
static void Optimize() {
  constexpr float LOW = 9.90e37;
  constexpr float HIGH = 1e38;

  printf("Search %.11g to %.11g\n", LOW, HIGH);
  
  GradOptimizer optimizer(OptimizeMe);
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

template<class F>
[[maybe_unused]]
static void Graph(F fn, float scale, float off) {
  Bounds bounds, error_bounds, nonlinear_bounds;
  double total_diff = 0.0;
  vector<double> samples, error_samples, nonlinear_samples;
  for (int i = 0; i < SAMPLES; i++) {
	// in [-1, 1]
	float in = (i / (float)(SAMPLES - 1)) * 2.0f - 1.0f;
	float out = fn(off, scale, in);
	double diff = (out - in);
	samples.push_back(out);
	bounds.Bound(i, out);
	error_samples.push_back(diff);
	error_bounds.Bound(i, diff);
	// printf("%.19g\n", diff);
  }
  printf("Total diff: %.19g\n", total_diff);

  // Compare to a linear interpolation of the first and last
  // endpoints.
  {
	double f0 = samples[0];
	double rise = (double)samples[SAMPLES - 1] - f0;
	double error = 0.0;
	for (int i = 0; i < SAMPLES; i++) {
	  double frac = i / (double)(SAMPLES - 1);
	  double linear = frac * rise;
	  double diff = samples[i] - linear;
	  error += diff * diff;
	  nonlinear_samples.push_back(diff);
	  nonlinear_bounds.Bound(i, diff);
	}
	printf("Squared error vs linear: %.19g\n", error);
  }

  
  constexpr int WIDTH = 512, HEIGHT = 512;
  printf("Bounds %.3f %.3f to %.3f %.3f\n",
         bounds.MinX(), bounds.MinY(),
         bounds.MaxX(), bounds.MaxY());
  printf("Error_Bounds %.11g %.11g to %.11g %.11g\n",
         error_bounds.MinX(), error_bounds.MinY(),
         error_bounds.MaxX(), error_bounds.MaxY());

  printf("Nonlinear_Bounds %.11g %.11g to %.11g %.11g\n",
         nonlinear_bounds.MinX(), nonlinear_bounds.MinY(),
         nonlinear_bounds.MaxX(), nonlinear_bounds.MaxY());

  bounds.AddMarginsFrac(0.01);
  error_bounds.AddMarginsFrac(0.01);
  nonlinear_bounds.AddMarginsFrac(0.01);  
  
  Bounds::Scaler scaler = bounds.Stretch(WIDTH, HEIGHT).FlipY();
  Bounds::Scaler error_scaler = error_bounds.Stretch(WIDTH, HEIGHT).FlipY();
  Bounds::Scaler nonlinear_scaler =
	nonlinear_bounds.Stretch(WIDTH, HEIGHT).FlipY();  
  
  {
    ImageRGBA img(WIDTH, HEIGHT);
    img.Clear32(0x000000FF);

    const int yaxis = error_scaler.ScaleY(0);
    img.BlendLine32(0, yaxis, WIDTH - 1, yaxis, 0xFFFFFF3F);

	bool lo = false;
	for (int x = 0; x < WIDTH; x += 10) {
	  img.BlendLine32(x, 0, x, HEIGHT - 1,
					  lo ? 0xFFFFFF11 : 0xFFFFFF22);
	  lo = !lo;
	}
	
	/*
    for (int x = 0; x <= max_scale; x++) {
      int xx = scaler.ScaleX(x);
      img.BlendLine32(xx, 0, xx, HEIGHT - 1, 0xFFFFFF3F);
      img.BlendText32(xx + 3, yaxis - 12, 0xFFFFFF7F,
                      StringPrintf("%d", x));
    }
	*/

	auto Plot = [&img](const vector<double> &samples,
					   const Bounds::Scaler &scaler,
					   uint32_t rgb) {
		for (int i = 0; i < samples.size(); i++) {
		  double d = samples[i];
		  int x = round(scaler.ScaleX(i));
		  int y = round(scaler.ScaleY(d));

		  // img.BlendBox32(x - 1, y - 1, 3, 3, rgb | 0x7F, {rgb | 0x3F});
		  img.BlendPixel32(x, y, rgb | 0xEE);
		}
	  };

	Plot(samples, scaler, 0x7FFF7F00);
	Plot(error_samples, error_scaler, 0xFF7F7F00);
	Plot(nonlinear_samples, nonlinear_scaler, 0x7F7FFF00);	

    string filename = "grad.png";
    img.Save(filename);
  }
}

int main(int argc, char **argv) {
  // Optimize();
  if (false) {
	// Error_Bounds 0 -5.9604644775e-08 to 999 5.9604644775e-08
	static constexpr float SCALE = 6.0077242583987507e+37;
	static constexpr float OFF = 9.9159268948476343e+37;
	Graph(Function2, SCALE, OFF);
  }

  if (false) {
	// static constexpr float SCALE = 9.9260844311214201e+37;
	// static constexpr float OFF = 9.9630854128974192e+37;
	static constexpr float SCALE = 9.9260844311214201e+37f;
	static constexpr float OFF = 9.9630854128974192e+37f;
	Graph(Function3, SCALE, OFF);
  }

  if (true) {
	// static constexpr float SCALE = 9.9260844311214201e+37;
	// static constexpr float OFF = 9.9630854128974192e+37;
	static constexpr float SCALE = 9.9684294429838515e+37;
	static constexpr float OFF = 9.9438839619657644e+37;
	Graph(Function3, SCALE, OFF);
  }

  return 0;
}
