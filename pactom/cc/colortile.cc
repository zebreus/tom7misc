
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <variant>
#include <utility>
#include <optional>
#include <functional>
#include <cmath>
#include <numbers>
#include <mutex>

#include "yocto_geometry.h"
#include "threadutil.h"
#include "image.h"
#include "color-util.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "timer.h"
#include "randutil.h"
#include "arcfour.h"
#include "opt/opt.h"
#include "pactom.h"
#include "lines.h"
#include "threadutil.h"

static constexpr double PI = std::numbers::pi;

static constexpr int THREADS = 16;

static ImageRGBA *background = nullptr;
static ImageRGBA *tile = nullptr;

static inline std::tuple<double, double, double> Recolor(
    uint32 color,
    double ho, double so, double vo) {
  auto [ro, go, bo, ao_] = ColorUtil::U32ToFloats(color);
  float h, s, v;
  std::tie(h, s, v) = ColorUtil::RGBToHSV(ro, go, bo);

  // Hue should wrap around.
  h += ho;
  if (h < 0.0) h += 1.0;
  if (h > 1.0) h -= 1.0;
  // But these should saturate.
  s = std::clamp(s + so, 0.0, 1.0);
  v = std::clamp(v + vo, 0.0, 1.0);

  return ColorUtil::HSVToRGB(h, s, v);
}

double OptimizeMe(double ho, double so, double vo) {
  std::mutex error_m;
  double error = 0.0;
  CHECK(tile->Height() == background->Height());
  CHECK(tile->Width() == background->Width());

  ParallelComp(tile->Height(),
             [&error_m, &error, ho, so, vo](int y) {
               double lerror = 0.0;
               for (int x = 0; x < tile->Width(); x++) {
                 uint32_t target = background->GetPixel32(x, y);
                 uint32_t orig = tile->GetPixel32(x, y);

                 // This is the adjusted color.
                 const auto [r, g, b] = Recolor(orig, ho, so, vo);

                 // Convert both to LAB for Delta-E.
                 const auto [tr, tg, tb, ta_] = ColorUtil::U32ToFloats(target);
                 const auto [l0, a0, b0] = ColorUtil::RGBToLAB(tr, tg, tb);
                 const auto [l1, a1, b1] = ColorUtil::RGBToLAB(r, g, b);

                 lerror += ColorUtil::DeltaE(l0, a0, b0,
                                            l1, a1, b1);
               }

               {
                 MutexLock ml(&error_m);
                 error += lerror;
               }
             },
             THREADS);

  printf("%.5f,%.5f,%.5f: %.5f\n", ho, so, vo, error);
  return error;
}

static std::tuple<double, double, double> Optimize() {
  auto [ho, so, vo] =
    Opt::Minimize3D(OptimizeMe,
                    std::make_tuple(-0.5, -0.5, -0.5),
                    std::make_tuple(0.5, 0.5, 0.5),
                    1000, 1, 10).first;
  printf("Best:\n"
         "const double ho = %.11f;\n"
         "const double so = %.11f;\n"
         "const double vo = %.11f;\n",
         ho, so, vo);

  return std::make_tuple(ho, so, vo);
}

int main(int argc, char **argv) {
  background = ImageRGBA::Load("recolor-bg.png");
  CHECK(background != nullptr);
  tile = ImageRGBA::Load("recolor-tile.png");
  CHECK(tile != nullptr);

  Timer opt_timer;
  const auto [ho, so, vo] = Optimize();
  printf("Optimized in %.3fs\n", opt_timer.Seconds());

  for (int y = 0; y < tile->Height(); y++) {
    for (int x = 0; x < tile->Width(); x++) {
      uint32 c = tile->GetPixel32(x, y);

      const auto [r, g, b] = Recolor(c, ho, so, vo);
      uint32 cc = ColorUtil::FloatsTo32(r, g, b, 1.0f);
      tile->SetPixel32(x, y, cc);
    }
  }

  tile->Save("tile-recolored.png");

  delete background;
  delete tile;
  return 0;
}
