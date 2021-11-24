
#ifndef _PLUGINVERT_PLUGINS_H
#define _PLUGINVERT_PLUGINS_H

#include <numbers>
#include <vector>
#include <cmath>

#include "base/logging.h"

struct Param {
  const char *name = nullptr;

  // Parameter lower and upper bounds.
  float lb = 0.0;
  float ub = 0.0;

  // Shape parameters for the probability density function for this
  // parameter. This describes a Beta distribution, which we scale
  // to the range [lb, ub]. Values of 1,1 yield a uniform
  // probability, which is a fine choice. Values 1,x includes the
  // possibility that the value is exactly lb (and this will be the
  // most likely value); symmetrically x,1 yields a PDF where the
  // value can be exactly ub. With a and b > 1, both endpoints will
  // have probability zero and the mean will be a/(a+b); the higher
  // the scale of a and b, the smaller the standard deviation.
  // plot-beta can help you try out some distributions.
  float beta_a = 1.0;
  float beta_b = 1.0;
};


template<int WINDOW_SIZE>
struct SimpleHighpass {
  static constexpr float SAMPLE_RATE = 44100.0f;
  static constexpr float PI = std::numbers::pi_v<float>;

  static constexpr int NUM_PARAMETERS = 2;

  static constexpr std::array<Param, NUM_PARAMETERS>
  PARAMS = {
    Param{.name = "cutoff",
          .lb = 20.0f,
          .ub = 20500.0f,
          .beta_a = 1.00200f,
          .beta_b = 1.40800f,
    },
    Param{.name = "q",
          .lb = 0.10f,
          .ub = 100.0f,
          .beta_a = 1.0f,
          .beta_b = 1.0f,
    },
  };

  static
  std::vector<float> Process(const std::vector<float> &in,
                             const std::array<float, NUM_PARAMETERS> &params) {
    // it don't work :(
    const float f0 = params[0];
    const float q = params[1];
    const float w0 = 2.0 * PI * f0 / SAMPLE_RATE;
    const float alpha = sin(w0) / (2.0 * q);
    const float a0 = 1.0 + alpha;
    const float a1 = -2.0 * cos(w0);
    const float a2 = 1.0 - alpha;
    const float b0 = (1.0 + cos(w0)) * 0.5f;
    const float b1 = -(1.0f + cos(w0));
    const float b2 = (1.0 + cos(w0)) * 0.5f;

    CHECK(in.size() == WINDOW_SIZE);
    std::vector<float> out(WINDOW_SIZE, 0.0f);
    out[0] = in[0];
    out[1] = in[1];
    for (int i = 2; i < WINDOW_SIZE; i++) {
      out[i] = ((b0 / a0) * in[i]) +
        ((b1 / a0) * in[i - 1]) +
        ((b2 / a0) * in[i - 2]) -
        ((a1 / a0) * out[i - 1]) -
        ((a2 / a0) * out[i - 2]);
    }
    return out;
  }

};


template<int WINDOW_SIZE>
struct Decimate {
  static constexpr int NUM_PARAMETERS = 1;

  static constexpr std::array<Param, NUM_PARAMETERS>
  PARAMS = {
    Param{.name = "divisor",
          .lb = 1.0f,
          .ub = 100.0f,
          .beta_a = 1.1f,
          .beta_b = 2.0f,
    },
  };

  static
  std::vector<float> Process(const std::vector<float> &in,
                             const std::array<float, NUM_PARAMETERS> &params) {
    std::vector<float> out(WINDOW_SIZE, 0.0f);
    const float p = params[0];
    for (int i = 0; i < WINDOW_SIZE; i++) {
      int s = round(in[i] * p);
      out[i] = s / p;
    }
    return out;
  }

};

#endif
