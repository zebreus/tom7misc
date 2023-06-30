
#ifndef _GRAD_GRAD_UTIL_H
#define _GRAD_GRAD_UTIL_H

#include <memory>
#include <cstdint>
#include <bit>

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

using uint32 = uint32_t;
using uint16 = uint16_t;
using half_float::half;
using namespace half_float::literal;

struct GradUtil {
  static constexpr ColorUtil::Gradient GREEN_BLUE {
    GradRGB(0.0f,  0x00FF00),
    GradRGB(1.0f,  0x0000FF),
  };

  // Non-finite ranges for half: 7c00-7fff
  // fc00-ffff
  // So this representation is probably bad
  // to search over as integers, since it
  // is not monotonic and has two holes in it.

  static constexpr inline uint16_t GetU16(half_float::half h) {
    return std::bit_cast<uint16_t, half_float::half>(h);
  }

  static constexpr inline half_float::half GetHalf(uint16_t u) {
    return std::bit_cast<half_float::half, uint16_t>(u);
  }

  // Can use bit_cast for these too.
  static inline uint32_t PackFloat(float f) {
    static_assert(sizeof (float) == 4);
    uint32_t ret = 0;
    std::memcpy(&ret, &f, sizeof (float));
    return ret;
  }

  static inline float UnpackFloat(uint32_t u) {
    static_assert(sizeof (float) == 4);
    float ret = 0.0f;
    std::memcpy(&ret, &u, sizeof (float));
    return ret;
  }

  // Range of all u16s in [-1, +1].
  static constexpr uint16 POS_LOW  = 0x0000; // +0
  static constexpr uint16 POS_HIGH = 0x3C00; // +1
  static constexpr uint16 NEG_LOW  = 0x8000; // -0
  static constexpr uint16 NEG_HIGH = 0xBC00; // -1

  // Run f on all u16s in [-1, +1].
  // This runs in magnitude order; use ForNeg1To1Ascending
  // for number line order.
  template<class F>
  static inline void ForPosNeg1(F f) {
    // Negative
    for (int u = NEG_LOW; u <= NEG_HIGH; u++)
      f((uint16)u);
    // Positive
    for (int u = POS_LOW; u <= POS_HIGH; u++)
      f((uint16)u);
  }

  template<class F>
  static inline void ForEveryFinite16(F f) {
    // Positive
    for (int u = 0; u < 0x7c00; u++)
      f((uint16)u);
    // Negative
    for (int u = 0x8000; u < 0xfc00; u++)
      f((uint16)u);
  }

  template<class F>
  static inline void ForNeg1To1Ascending(F f) {
    // Negative
    for (int u = NEG_HIGH; u >= NEG_LOW; u--)
      f((uint16)u);
    // Positive
    for (int u = POS_LOW; u <= POS_HIGH; u++)
      f((uint16)u);
  }

  // The function (uint16 -> uint16) can be completely described as a table
  // of the resulting value for the 65536 inputs.
  using Table = std::array<uint16_t, 65536>;

  static void Graph(const Table &table, uint32 color, ImageRGBA *img,
                    // XXX HAX
                    int dy = 0) {
    CHECK(img->Width() == img->Height());
    const int size = img->Width();

    // Loop over [-1, 1].
    auto Plot = [&](uint16 input) {
        uint16 output = table[input];
        double x = GetHalf(input);
        double y = GetHalf(output);

        int xs = (int)std::round((size / 2.0) + x * (size / 2.0));
        int ys = (int)std::round((size / 2.0) + -y * (size / 2.0)) + dy;

        // ys = std::clamp(ys, 0, size - 1);

        uint32 c = color;
        /*
        if (x < -1.0f) c = 0xFF000022;
        else if (x > 1.0f) c = 0x00FF0022;
        */
        img->BlendPixel32(xs, ys, c);
      };

    /*
    for (int i = NEG_LOW; i < NEG_HIGH; i++) Plot(i);
    for (int i = POS_LOW; i < POS_HIGH; i++) Plot(i);
    */
    ForEveryFinite16(Plot);
  }

  template<size_t PX>
  static void GraphPx(const Table &table, uint32 color, ImageRGBA *img) {
    CHECK(img->Width() == img->Height());
    const int size = img->Width();

    // Loop over [-1, 1].
    auto Plot = [&](uint16 input) {
        uint16 output = table[input];
        double x = GetHalf(input);
        double y = GetHalf(output);

        int xs = (int)std::round((size / 2.0) + x * (size / 2.0));
        int ys = (int)std::round((size / 2.0) + -y * (size / 2.0));

        for (int yy = -(int)PX; yy <= (int)PX; yy++) {
          for (int xx = -(int)PX; xx <= (int)PX; xx++) {
            constexpr size_t SPX = PX * PX;
            if (xx * xx + yy * yy <= SPX) {
              img->BlendPixel32(xs + xx, ys + yy, color);
            }
          }
        }
      };

    ForEveryFinite16(Plot);
  }


  static void GridWithBounds(const Bounds &bounds, ImageRGBA *img,
                             double xtick = 0.125,
                             double ytick = 0.125) {
    Bounds::Scaler scaler = bounds.Stretch(img->Width(), img->Height()).FlipY();

    // In native coordinates.
    double top = scaler.UnscaleY(0.0);
    double bot = scaler.UnscaleY(img->Height());

    double left = scaler.UnscaleX(0.0);
    double right = scaler.UnscaleX(img->Width());

    {
      // ticks on x axis
      int parity = 0;
      for (double x = xtick; x < right || -x > left; x += xtick) {
        // Positive and negative ticks at once.
        double px = scaler.ScaleX(x);
        double nx = scaler.ScaleX(-x);
        double y0 = scaler.ScaleY(top);
        double y1 = scaler.ScaleY(bot);
        const uint32_t color = (parity == 0) ? 0xFFFFFF22 : 0xFFFFFF11;
        img->BlendLine32(px, y0, px, y1, color);
        img->BlendLine32(nx, y0, nx, y1, color);
        parity++;
        parity &= 3;
      }
    }

    {
      // ticks on y axis
      int parity = 0;
      for (double y = ytick; y < top || -y > bot; y += ytick) {
        double py = scaler.ScaleY(y);
        double ny = scaler.ScaleY(-y);
        double x0 = scaler.ScaleX(left);
        double x1 = scaler.ScaleX(right);
        const uint32_t color = (parity == 0) ? 0xFFFFFF22 : 0xFFFFFF11;
        img->BlendLine32(x0, py, x1, py, color);
        img->BlendLine32(x0, ny, x1, ny, color);
        parity++;
        parity &= 3;
      }
    }

    {
      // y axis
      const auto [x0, y0] = scaler.Scale(0.0, top);
      const auto [x1, y1] = scaler.Scale(0.0, bot);
      img->BlendLine32(x0, y0, x1, y1, 0xFFFFFF22);
    }

    {
      // x axis
      const auto [x0, y0] = scaler.Scale(left, 0.0);
      const auto [x1, y1] = scaler.Scale(right, 0.0);
      img->BlendLine32(x0, y0, x1, y1, 0xFFFFFF22);
    }

  }

  static void GraphWithBounds(
      const Bounds &bounds,
      const Table &table, uint32 color, ImageRGBA *img) {

    Bounds::Scaler scaler =
      bounds.Stretch(img->Width(), img->Height()).FlipY();

    // Plot a single value
    auto Plot = [&](uint16 input) {
        uint16 output = table[input];
        double x = GetHalf(input);
        double y = GetHalf(output);

        auto [xs, ys] = scaler.Scale(x, y);

        uint32 c = color;
        img->BlendPixel32((int)std::round(xs), (int)std::round(ys), c);
      };

    // Plot everything, even if off screen.
    ForEveryFinite16(Plot);
  }


  static void Grid(ImageRGBA *img) {
    CHECK(img->Width() == img->Height());
    const int size = img->Width();

    auto MapCoord = [size](double x, double y) -> pair<int, int> {
      int xs = (int)std::round((size / 2) + x * (size / 2));
      int ys = (int)std::round((size / 2) + -y * (size / 2));
      return make_pair(xs, ys);
    };

    int parity = 0;
    for (double y = -1.0; y <= 1.0; y += 0.0625) {
      const auto [x0, y0] = MapCoord(-1.0, y);
      const auto [x1, y1] = MapCoord(+1.0, y);
      const uint32_t color = (parity == 0) ? 0xFFFFFF22 : 0xFFFFFF11;
      img->BlendLine32(x0, y0, x1, y1, color);
      parity++;
      parity &= 3;
    }

    parity = 0;
    for (double x = -1.0; x <= 1.0; x += 0.0625) {
      const auto [x0, y0] = MapCoord(x, -1.0);
      const auto [x1, y1] = MapCoord(x, +1.0);
      const uint32_t color = (parity == 0) ? 0xFFFFFF22 : 0xFFFFFF11;
      img->BlendLine32(x0, y0, x1, y1, color);
      parity++;
      parity &= 3;
    }

    {
      // y axis
      const auto [x0, y0] = MapCoord(0.0, -1.0);
      const auto [x1, y1] = MapCoord(0.0, +1.0);
      img->BlendLine32(x0, y0, x1, y1, 0xFFFFFF22);
    }

    {
      // x axis
      const auto [x0, y0] = MapCoord(-1.0, 0.0);
      const auto [x1, y1] = MapCoord(+1.0, 0.0);
      img->BlendLine32(x0, y0, x1, y1, 0xFFFFFF22);
    }
  }

  static void UpdateTable(Table *table, const std::function<half(half)> &f) {
    for (uint16 &u : *table) {
      half in = GetHalf(u);
      half out = f(in);
      u = GetU16(out);
    }
  }

  static void UpdateTable16(Table *table,
                            const std::function<uint16(uint16)> &f) {
    for (int i = 0; i < table->size(); i++) {
      // printf("%d\n", i);
      (*table)[i] = f((*table)[i]);
    }
  }

  static Table IdentityTable() {
    Table table;
    for (int i = 0; i < 65536; i++) {
      table[i] = i;
    }
    return table;
  }

  // TODO: Replace this with the representation in state.h.
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

  static string StepString(const Step &step) {
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

  // With functions f and g, return h(x) = f(x) - g(x)
  static Table Minus(const Table &f, const Table &g) {
    Table out;
    for (int i = 0; i < 65536; i++) {
      uint16 x = (uint16)i;
      half fh = GetHalf(f[x]);
      half gh = GetHalf(g[x]);
      uint16 y = GetU16(fh - gh);
      out[x] = y;
    }
    return out;
  }

  // 500 iterations of multiplying by 0.99951171875, which
  // is the first number smaller than one. Then rescale back
  // to the original scale.
  static State MakeTable1() {
    static constexpr uint16 C = 0x3bffu;
    static constexpr int ITERS = 500;
    State state;
    for (int i = 0; i < ITERS; i++) {
      ApplyStep(Step{.mult = true, .value = C}, &state.table);
    }

    // And recenter.
    [[maybe_unused]] static constexpr uint16 OFF = 0x8000;
    static constexpr uint16 SCALE = 0x3d4b;

    ApplyStep(Step{.mult = true, .value = SCALE}, &state.table);
    return state;

    /*
    // This should produce the same result as above.
    const auto &[offset, scale] = Recentering(state.table);
    printf("Table1 offset %04x scale %04x\n",
            GetU16(offset), GetU16(scale));
     printf("Offset %.9g Scale %.9g\n", (float)offset, (float)scale);

     ApplyStep(Step{.mult = false,
                   .value = GradUtil::GetU16(offset)},
      &state.table);
    ApplyStep(Step{.mult = true,
                   .value = GradUtil::GetU16(scale)},
      &state.table);
    return state;
    */
  }

  // This is a lot like the leaky relu function (between [-1, 1] at least)
  // although the knee is not as pronounced. There's also significant
  // discretization error.
  static State MakeTable2() {
    // This is -4.
    static constexpr uint16 OFF = 0xc400;
    // This is the first number smaller than 1.0.
    static constexpr uint16 C = 0x3bffu;
    static constexpr int ITERS = 300;
    State state;

    GradUtil::ApplyStep(Step{.mult = false, .value = OFF},
                        &state.table);

    for (int i = 0; i < ITERS; i++) {
      ApplyStep(Step{.mult = true, .value = C}, &state.table);
    }

    // And recenter.
    const auto &[offset, scale] = Recentering(state.table);
    // printf("Table2 offset %04x scale %04x\n",
    // GetU16(offset), GetU16(scale));

    // printf("Offset %.9g Scale %.9g\n", (float)offset, (float)scale);
    ApplyStep(Step{.mult = false,
                   .value = GradUtil::GetU16(offset)},
      &state.table);
    ApplyStep(Step{.mult = true,
                   .value = GradUtil::GetU16(scale)},
      &state.table);
    return state;
  }

  static void SaveToImage(int size, const Table &table,
                          const std::string &filename) {
    ImageRGBA img(size, size);
    img.Clear32(0x000000FF);
    Grid(&img);
    Graph(table, 0xFFFFFF77, &img);
    img.Save(filename);
  }

  static void SaveFunctionToFile(
      const Table &table,
      const std::string &filename) {
    ImageRGBA img(256, 256);
    for (int i = 0; i < 65536; i++) {
      uint16_t o = table[i];
      uint8_t r = (o >> 8) & 255;
      uint8_t g = o & 255;
      int y = i >> 8;
      int x = i & 255;
      img.SetPixel(x, y, r, g, 0x00, 0xFF);
    }
    img.Save(filename);
  }

  static std::vector<uint16_t> GetFunctionFromFile(
      const std::string &filename) {
    std::vector<uint16_t> ret(65536, 0);
    std::unique_ptr<ImageRGBA> img(ImageRGBA::Load(filename));
    CHECK(img.get() != nullptr);
    CHECK(img->Width() == 256 && img->Height() == 256);
    for (int i = 0; i < 65536; i++) {
      const int y = i / 256;
      const int x = i % 256;
      const auto [r, g, b, a] = img->GetPixel(x, y);
      CHECK(b == 0 && a == 0xFF);
      ret[i] = ((uint16_t)r << 8) | g;
    }
    return ret;
  }

  static std::vector<float> GetDerivativeFromFile(
      const std::string &filename) {
    std::vector<float> ret(65536, 0.0f);
    std::unique_ptr<ImageRGBA> img(ImageRGBA::Load(filename));
    CHECK(img.get() != nullptr) << filename;
    CHECK(img->Width() == 256 && img->Height() == 256) << filename;
    // int nonzero = 0;
    for (int i = 0; i < 65536; i++) {
      const int y = i / 256;
      const int x = i % 256;
      uint32 u = img->GetPixel32(x, y);
      // if (u) nonzero++;
      ret[i] = UnpackFloat(u);
      // printf("%d: %04x %.5f\n", i, u, ret[i]);
    }
    // printf("Num nonzero: %d\n", nonzero);
    return ret;
  }

};

#endif

