#ifndef _CC_LIB_COLOR_UTIL_H
#define _CC_LIB_COLOR_UTIL_H

#include <tuple>
#include <cstdint>
#include <vector>
#include <initializer_list>

// Convenience for specifying rows in gradients as 0xRRGGBB.
// This should be a member of ColorUtil but is not allowed, perhaps
// because of a compiler bug?
static inline constexpr
std::tuple<float, float, float, float> GradRGB(float f, uint32_t rgb) {
  return std::tuple<float, float, float, float>(
      f,
      ((rgb >> 16) & 255) / 255.0f,
      ((rgb >>  8) & 255) / 255.0f,
      ( rgb        & 255) / 255.0f);
}

struct ColorUtil {
  // hue, saturation, value nominally in [0, 1].
  // hue of 0.0 is red.
  // RGB output also in [0, 1].
  static void HSVToRGB(float hue, float saturation, float value,
                       float *r, float *g, float *b);
  static std::tuple<float, float, float>
  HSVToRGB(float hue, float saturation, float value);

  // Convert to CIE L*A*B*.
  // RGB channels are nominally in [0, 1].
  // Here RGB is interpreted as sRGB with a D65 reference white.
  // L* is nominally [0,100]. A* and B* are unbounded but
  // are "commonly clamped to -128 to 127".
  static void RGBToLAB(float r, float g, float b,
                       float *ll, float *aa, float *bb);
  static std::tuple<float, float, float>
  RGBToLAB(float r, float g, float b);

  // CIE1994 distance between sample color Lab2 and reference Lab1.
  // ** Careful: This may not even be symmetric! **
  // Note: This has been superseded by an even more complicated function
  // (CIEDE2000) if you are doing something very sensitive.
  static float DeltaE(float l1, float a1, float b1,
                      float l2, float a2, float b2);

  // initializer_list so that these can be constexpr.
  using Gradient = std::initializer_list<
    std::tuple<float, float, float, float>>;

  // Three-channel linear gradient with the sample point t and the
  // gradient ramp specified. Typical usage would be for t in [0,1]
  // and RGB channels each in [0,1], but other uses are acceptable
  // (everything is just Euclidean). For t outside the range, the
  // endpoints are returned.
  static std::tuple<float, float, float>
  LinearGradient(const Gradient &ramp, float t);

  // As RGBA, with alpha=0xFF.
  static uint32_t LinearGradient32(const Gradient &ramp, float t);

  static constexpr Gradient HEATED_METAL{
    GradRGB(0.0f,  0x000000),
    GradRGB(0.2f,  0x7700BB),
    GradRGB(0.5f,  0xFF0000),
    GradRGB(0.8f,  0xFFFF00),
    GradRGB(1.0f,  0xFFFFFF)
  };

};


#endif

