
#include "pactom.h"

#include <string>
#include <vector>
#include <memory>
#include <cstdint>

#include "base/logging.h"
#include "geom/latlon.h"
#include "bounds.h"
#include "image.h"
#include "lines.h"
#include "arcfour.h"
#include "randutil.h"
#include "color-util.h"
#include "ansi.h"

#include "pactom-util.h"

using namespace std;


using int64 = int64_t;

static constexpr double METERS_TO_FEET = 3.28084;
static constexpr int WIDTH = 1920 * 3;
static constexpr int HEIGHT = 1080 * 3;
static constexpr int SCALE = 4;
// Additional pixels to draw for line (0 = 1 pixel thick)
static constexpr int RADIUS = 2 * 2;

static bool HasZeroElev(const std::vector<std::pair<LatLon, double>> &path) {
  for (const auto &[ll, elev] : path)
    if (elev == 0.0)
      return true;
  return false;
}

int main(int argc, char **argv) {
  AnsiInit();

  ArcFour rc("pactom");
  unique_ptr<PacTom> pactom = PacTomUtil::Load(true);
  CHECK(pactom.get() != nullptr);
  PacTomUtil::SortByDate(pactom.get());

  // Find the extrema.
  Bounds bounds;
  int has_zero_elev = 0;
  for (const auto &r : pactom->runs) {
    if (r.path.empty()) continue;
    if (HasZeroElev(r.path)) {
      has_zero_elev++;
      continue;
    }
    double dist = 0.0;
    LatLon prev = r.path[0].first;
    for (const auto &[latlon, elev] : r.path) {
      dist += LatLon::DistMeters(prev, latlon);
      bounds.Bound(dist, elev);
      prev = latlon;
    }
  }
  bounds.AddMarginFrac(0.05);
  printf("Has zero elev: %d/%d\n", has_zero_elev, (int)pactom->runs.size());

  Bounds::Scaler scaler = bounds.Stretch(WIDTH * SCALE,
                                         HEIGHT * SCALE).FlipY();

  ImageRGBA image(WIDTH * SCALE, HEIGHT * SCALE);
  image.Clear32(0x000000FF);

  for (int idx = 0; idx < pactom->runs.size(); idx++) {
    double frac = idx / (double)pactom->runs.size();
    const auto &r = pactom->runs[idx];
    if (r.path.empty() ||
        HasZeroElev(r.path)) continue;
    // const uint32_t color = PacTomUtil::RandomBrightColor(&rc) & 0xFFFFFF33;
    uint32_t color = ColorUtil::LinearGradient32(ColorUtil::HEATED_METAL,
                                                 frac) & 0xFFFFFF33;
    double dist = 0.0;
    for (int i = 1; i < r.path.size(); i++) {
      const auto &[ll0, elev0] = r.path[i - 1];
      const auto &[ll1, elev1] = r.path[i];

      auto [x0, y0] = scaler.Scale(dist, elev0);
      dist += LatLon::DistMeters(ll0, ll1);
      auto [x1, y1] = scaler.Scale(dist, elev1);

      PacTomUtil::DrawThickLine<RADIUS>(&image,
                                        x0, y0,
                                        x1, y1, color);
    }
  }



  ImageRGBA out = image.ScaleDownBy(SCALE);

  out.Save("elevation.png");
  printf("Wrote elevation.png.\n");

  return 0;
}
