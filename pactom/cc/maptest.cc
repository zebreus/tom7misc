
#include "pactom.h"

#include <string>
#include <vector>
#include <memory>
#include <cstdint>

#include "base/logging.h"
#include "geom/latlon.h"
#include "bounds.h"
#include "image.h"

using namespace std;

using int64 = int64_t;

static constexpr int WIDTH = 1920;
static constexpr int HEIGHT = 1080;
static constexpr int SCALE = 1;

int main(int argc, char **argv) {

  unique_ptr<PacTom> pactom = PacTom::FromFiles({"../pac.kml",
                                                 "../pac2.kml"});
  CHECK(pactom.get() != nullptr);

  int64 pts = 0;
  for (auto &p : pactom->paths) pts += p.size();

  printf("Loaded %lld paths with %lld waypoints.\n",
         pactom->paths.size(), pts);

  const LatLon home = LatLon::FromDegs(40.452911, -79.936313);
  LatLon::Projection Project = LatLon::Gnomonic(home);
  // LatLon::Projection Project = LatLon::PlateCarree();

  // Find the extrema.
  Bounds bounds;
  for (const auto &p : pactom->paths) {
    for (const auto &[latlon, elev] : p) {
      auto [x, y] = Project(latlon);
      bounds.Bound(x, y);
    }
  }
  bounds.AddMarginFrac(0.05);

  // XXX ScaleToFit!
  Bounds::Scaler scaler = bounds.Stretch(WIDTH * SCALE,
                                         HEIGHT * SCALE).FlipY();

  ImageRGBA image(WIDTH * SCALE, HEIGHT * SCALE);
  image.Clear32(0x000000FF);
  for (const auto &p : pactom->paths) {
    for (int i = 0; i < p.size() - 1; i++) {
      const auto &[latlon0, elev0] = p[i];
      const auto &[latlon1, elev1] = p[i + 1];
      auto [x0, y0] = scaler.Scale(Project(latlon0));
      auto [x1, y1] = scaler.Scale(Project(latlon1));

      printf("%d %d -> %d %d\n", (int)x0, (int)y0, (int)x1, (int)y1);

      image.BlendLine32(x0, y0, x1, y1, 0xFFFFFFFF);
    }
  }

  ImageRGBA out = image.ScaleDownBy(SCALE);

  out.Save("maptest.png");
  printf("Wrote maptest.png.\n");

  return 0;
}
