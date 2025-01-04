
#include "pactom.h"

#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <numbers>

#include "base/logging.h"
#include "base/stringprintf.h"
#include "geom/latlon.h"
#include "bounds.h"
#include "image.h"
#include "lines.h"
#include "arcfour.h"
#include "randutil.h"
#include "color-util.h"
#include "threadutil.h"
#include "pactom-util.h"

using namespace std;


using int64 = int64_t;

static constexpr int WIDTH = 1920;
static constexpr int HEIGHT = 1080;
static constexpr int SCALE = 2;

static constexpr double PI = std::numbers::pi;

int main(int argc, char **argv) {
  ArcFour rc("pactom");

  const LatLon home = LatLon::FromDegs(40.452911, -79.936313);
  LatLon::Projection Project = LatLon::Gnomonic(home);
  LatLon::InverseProjection InverseProj = LatLon::InverseGnomonic(home);
  const std::pair<double, double> home_pt = Project(home);

  // Find the extrema.
  Bounds bounds;
  {
    auto [x, y] = Project(LatLon::FromDegs(40.480688, -79.890350));
    bounds.Bound(x, y);
  }

  {
    auto [x, y] = Project(LatLon::FromDegs(40.400471, -80.074371));
    bounds.Bound(x, y);
  }


  Bounds::Scaler scaler = bounds.ScaleToFit(WIDTH * SCALE,
                                            HEIGHT * SCALE).FlipY();

  ImageRGBA image(WIDTH * SCALE, HEIGHT * SCALE);
  image.Clear32(0x000000FF);

  for (int y = 0; y < image.Height(); y++) {
    for (int x = 0; x < image.Width(); x++) {
      const auto [xx, yy] = scaler.Unscale(x, y);
      const LatLon pos = InverseProj(xx, yy);

      const optional<double> rhoo = LatLon::Angle(home, pos);
      if (!rhoo.has_value()) continue;
      auto [r, g, b] = ColorUtil::HSVToRGB(
          rhoo.value() / (PI * 2.0), 1.0, 1.0);
      uint32_t color = ColorUtil::FloatsTo32(r, g, b, 1.0f);
      image.SetPixel32(x, y, color);
    }
  }

  #if 1
  for (int i = 0; i < 40; i++) {
    int x = RandTo(&rc, image.Width());
    int y = RandTo(&rc, image.Height());

    const auto [xx, yy] = scaler.Unscale(x, y);
    const LatLon pos = InverseProj(xx, yy);

    const optional<double> rhoo = LatLon::Angle(pos, home);
    if (rhoo.has_value()) {
      image.BlendText2x32(x, y, 0x000000FF,
                          StringPrintf("%.3f", rhoo.value()));
    }
  }
  #endif

  ImageRGBA out = image.ScaleDownBy(SCALE);
  out.Save("angletest.png");
  return 0;
}
