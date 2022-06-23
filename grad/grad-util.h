
#ifndef _GRAD_GRAD_UTIL_H
#define _GRAD_GRAD_UTIL_H

#include "base/logging.h"
#include "base/stringprintf.h"

#include "util.h"
#include "image.h"
#include "bounds.h"
#include "opt/optimizer.h"
#include "half.h"
#include "color-util.h"

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
  static inline half GetHalf(uint16 u) {
    half h;
    static_assert(sizeof (h) == sizeof (u));
    memcpy((void*)&h, (void*)&u, sizeof (u));
    return h;
  }

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

  template<class F>
  static inline void ForEveryFinite16(F f) {
    for (int u = POS_LOW; u < POS_HIGH; u++)
      f((uint16)u);
    for (int u = NEG_LOW; u < NEG_HIGH; u++)
      f((uint16)u);
  }

  // The function (uint16 -> uint16) can be completely described as a table
  // of the resulting value for the 65536 inputs.
  using Table = std::array<uint16_t, 65536>;

  // XXX argument
  static void Graph(const Table &table, uint32 color, ImageRGBA *img) {
    CHECK(img->Width() == img->Height());
    const int size = img->Width();

    // Loop over [-1, 1].
    auto Plot = [&](uint16 input) {
        uint16 output = table[input];
        double x = GetHalf(input);
        double y = GetHalf(output);

        int xs = (int)std::round((size / 2) + x * (size / 2));
        int ys = (int)std::round((size / 2) + -y * (size / 2));

        ys = std::clamp(ys, 0, size - 1);

        img->BlendPixel32(xs, ys, color);
      };

    for (int i = NEG_LOW; i < NEG_HIGH; i++) Plot(i);
    for (int i = POS_LOW; i < POS_HIGH; i++) Plot(i);
  }

  static void Grid(ImageRGBA *img) {
    CHECK(img->Width() == img->Height());
    const int size = img->Width();

    auto MapCoord = [size](double x, double y) -> pair<int, int> {
      int xs = (int)std::round((size / 2) + x * (size / 2));
      int ys = (int)std::round((size / 2) + -y * (size / 2));
      return make_pair(xs, ys);
    };

    for (double y = -1.0; y <= 1.0; y += 0.1) {
      const auto [x0, y0] = MapCoord(-1.0, y);
      const auto [x1, y1] = MapCoord(+1.0, y);
      img->BlendLine32(x0, y0, x1, y1, 0xFFFFFF11);
    }

    for (double x = -1.0; x <= 1.0; x += 0.1) {
      const auto [x0, y0] = MapCoord(x, -1.0);
      const auto [x1, y1] = MapCoord(x, +1.0);
      img->BlendLine32(x0, y0, x1, y1, 0xFFFFFF11);
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
};

#endif

