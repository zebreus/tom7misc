
#include "color-util.h"

#include <cmath>
#include <tuple>
#include <utility>

// TODO: std::clamp
template<class T>
static inline T Clamp(T value, T low, T high) {
  if (value < low) return low;
  if (value > high) return high;
  return value;
}

// static
std::tuple<float, float, float>
ColorUtil::HSVToRGB(float h, float s, float v) {
  if (s == 0.0f) {
    return std::make_tuple(v, v, v);
  } else {
    const float hue = h * 6.0f;
    const int fh = (int)hue;
    const float var_1 = v * (1 - s);
    const float var_2 = v * (1 - s * (hue - fh));
    const float var_3 = v * (1 - s * (1 - (hue - fh)));

    float red, green, blue;

    switch (fh) {
    case 0:  red = v;      green = var_3;  blue = var_1; break;
    case 1:  red = var_2;  green = v;      blue = var_1; break;
    case 2:  red = var_1;  green = v;      blue = var_3; break;
    case 3:  red = var_1;  green = var_2;  blue = v;     break;
    case 4:  red = var_3;  green = var_1;  blue = v;     break;
    default: red = v;      green = var_1;  blue = var_2; break;
    }

    return std::make_tuple(red, green, blue);
  }
}

// static
void ColorUtil::HSVToRGB(float h, float s, float v,
                         float *r, float *g, float *b) {
  std::tie(*r, *g, *b) = HSVToRGB(h, s, v);
}


static std::tuple<float, float, float>
sRGBToLAB(float srgb_r, float srgb_g, float srgb_b) {
  // Now to XYZ color space, whose components are nominally in [0, 1].
  // This is just a matrix multiply using a mysterious matrix. Here
  // using sRGB with D65 reference white.
  // http://www.brucelindbloom.com/Eqn_RGB_XYZ_Matrix.html
  const float x =
    srgb_r * 0.4124564f + srgb_g * 0.3575761f + srgb_b * 0.1804375f;
  const float y =
    srgb_r * 0.2126729f + srgb_g * 0.7151522f + srgb_b * 0.0721750f;
  const float z =
    srgb_r * 0.0193339f + srgb_g * 0.1191920f + srgb_b * 0.9503041f;

  // Reference white D65. This is what ColorMine uses and it produces
  // results more like I'd expect (e.g. RGB #FFFFFF gives LAB 100,0,0).
  // http://www.brucelindbloom.com/index.html?Eqn_RGB_to_XYZ.html
  [[maybe_unused]] constexpr float inv_white_x = 1.0f / 0.95047f;
  [[maybe_unused]] constexpr float inv_white_y = 1.0f;
  [[maybe_unused]] constexpr float inv_white_z = 1.0f / 1.08883f;

  auto F = [](float ch) {
    // Worth it just for the gif:
    // http://www.brucelindbloom.com/LContinuity.html
    static constexpr float epsilon = 216.0f / 24389.0f;
    static constexpr float kappa_div_116 = (24389.0f / 27.0f) / 116.0f;
    static constexpr float one_third = 1.0f / 3.0f;
    static constexpr float sixteen_116ths = 16.0f / 116.0f;
    return ch > epsilon ? powf(ch, one_third) :
      (kappa_div_116 * ch + sixteen_116ths);
  };

  const float fx = F(x * inv_white_x);
  const float fy = F(y /* * inv_white_y */);
  const float fz = F(z * inv_white_z);

  const float ll = (116.0f * fy) - 16.0f;
  const float aa = 500.0f * (fx - fy);
  const float bb = 200.0f * (fy - fz);
  return {ll, aa, bb};
}

// static
std::tuple<float, float, float>
ColorUtil::RGBToLAB(float r, float g, float b) {
  // First we need to un-compand the RGB triplet.
  // Technically there are different choices here, but sRGB is what we
  // really mean by RGB in this library.
  // http://www.brucelindbloom.com/Eqn_RGB_to_XYZ.html
  auto SRGBInvCompand = [](float ch) {
    static constexpr float inv1055 = 1.0f / 1.055f;
    static constexpr float inv1292 = 1.0f / 12.92f;
    return ch > 0.04045f ? powf((ch + 0.055f) * inv1055, 2.4f) : ch * inv1292;
  };
  const float srgb_r = SRGBInvCompand(r);
  const float srgb_g = SRGBInvCompand(g);
  const float srgb_b = SRGBInvCompand(b);

  return sRGBToLAB(srgb_r, srgb_g, srgb_b);
}

// static
void ColorUtil::RGBToLAB(float r, float g, float b,
                         float *ll, float *aa, float *bb) {
  std::tie(*ll, *aa, *bb) = RGBToLAB(r, g, b);
}

// static
float ColorUtil::DeltaE(float l1, float a1, float b1,
                        float l2, float a2, float b2) {
  const float dl = l1 - l2;
  const float da = a1 - a2;
  const float db = b1 - b2;
  const float c1 = sqrtf(a1 * a1 + b1 * b1);
  const float c2 = sqrtf(a2 * a2 + b2 * b2);
  const float dc = c1 - c2;
  const float dhsq = da * da + db * db - dc * dc;
  // later we have (dh / kh*sh)^2, which is
  // dh^2 / (kh * sh)^2,
  // so keep around dh^2 as dhsq instead of taking square root.
  const float sc = 1.0f + 0.045f * c1;
  const float sh = 1.0f + 0.015f * c1;
  const float v1 = dl;
  const float v2 = dc / sc;
  const float v3sq = dhsq / (sh * sh);

  const float de = v1 * v1 + v2 * v2 + v3sq;
  return de <= 0.0f ? 0.0f : sqrtf(de);
}

std::tuple<float, float, float>
ColorUtil::LinearGradient(
    const ColorUtil::Gradient &ramp,
    float t) {

  // Avoid dependency on logging, but this would be invalid.
  if (ramp.size() == 0) return std::make_tuple(0.0f, 0.0f, 0.0f);

  auto it = ramp.begin();
  auto prev = *it;
  ++it;

  {
    const auto [x, r, g, b] = prev;
    if (t < x) {
      return std::make_tuple(r, g, b);
    }
  }

  for (/* above */; it != ramp.end(); ++it) {
    const auto now = *it;
    const auto [px, pr, pg, pb] = prev;
    const auto [x, r, g, b] = now;
    if (t < x) {
     // linear interpolation
      const float w = x - px;
      const float f = (t - px) / w;
      const float omf = 1.0f - f;
      return std::make_tuple(f * r + omf * pr,
                             f * g + omf * pg,
                             f * b + omf * pb);
    }
    prev = now;
  }

  {
    const auto [x, r, g, b] = prev;
    return std::make_tuple(r, g, b);
  }
}

uint32_t ColorUtil::LinearGradient32(
    const ColorUtil::Gradient &ramp,
    float t) {
  const auto [rf, gf, bf] = LinearGradient(ramp, t);
  uint32_t r = Clamp((int)std::round(rf * 255.0f), 0, 255);
  uint32_t g = Clamp((int)std::round(gf * 255.0f), 0, 255);
  uint32_t b = Clamp((int)std::round(bf * 255.0f), 0, 255);
  return (r << 24) | (g << 16) | (b << 8) | 0xFF;
}
