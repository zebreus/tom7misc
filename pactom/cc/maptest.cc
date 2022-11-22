
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
#include "osm.h"

using namespace std;


using int64 = int64_t;

static constexpr int WIDTH = 1920;
static constexpr int HEIGHT = 1080;
static constexpr int SCALE = 4;
// Additional pixels to draw for line (0 = 1 pixel thick)
static constexpr int RADIUS = 2;

template<int RADIUS>
static void DrawThickLine(ImageRGBA *image,
                          int x0, int y0, int x1, int y1,
                          uint32_t color) {
  image->BlendPixel32(x0, y0, color);
  for (const auto [x, y] : Line<int>{(int)x0, (int)y0, (int)x1, (int)y1}) {
    for (int dy = -RADIUS; dy <= RADIUS; dy++) {
      const int ddy = dy * dy;
      for (int dx = -RADIUS; dx <= RADIUS; dx++) {
        const int ddx = dx * dx;
        if (ddy + ddx <= RADIUS * RADIUS) {
          image->BlendPixel32(x + dx, y + dy, color);
        }
      }
    }
  }
}

static uint32 RandomBrightColor(ArcFour *rc) {
  const float h = RandFloat(rc);
  const float s = 0.5f + (0.5f * RandFloat(rc));
  const float v = 0.5f + (0.5f * RandFloat(rc));
  float r, g, b;
  ColorUtil::HSVToRGB(h, s, v, &r, &g, &b);
  const uint32 rr = std::clamp((int)roundf(r * 255.0f), 0, 255);
  const uint32 gg = std::clamp((int)roundf(g * 255.0f), 0, 255);
  const uint32 bb = std::clamp((int)roundf(b * 255.0f), 0, 255);

  return (rr << 24) | (gg << 16) | (bb << 8) | 0xFF;
}

static bool DrawRoad(OSM::Highway highway) {
  return highway != OSM::NONE;
}

inline static uint32_t WayColor(OSM::Highway highway) {
  switch (highway) {
    // case OSM::UNCLASSIFIED: return 0x0000FFAA;
    // case OSM::OTHER: return 0x00FF00AA;
  default: return 0x602020AA;
  }
}

int main(int argc, char **argv) {
  ArcFour rc("pactom");
  unique_ptr<PacTom> pactom = PacTom::FromFiles({"../pac.kml",
                                                 "../pac2.kml"},
    "../neighborhoods.kml"
    );
  CHECK(pactom.get() != nullptr);

  double path_feet = 0.0;
  for (const auto &p : pactom->paths) {
    for (int i = 0; i < p.size() - 1; i++) {
      const auto &[latlon0, elev0] = p[i];
      const auto &[latlon1, elev1] = p[i + 1];
      path_feet += LatLon::DistFeet(latlon0, latlon1);
    }
  }
  printf("Total miles: %.6f\n", path_feet / 5280.0);

  OSM osm;
  for (const string osmfile : {
      "../pittsburgh-center.osm",
      "../pittsburgh-northeast.osm",
      "../pittsburgh-north.osm",
      "../pittsburgh-south.osm",
      "../pittsburgh-southwest.osm",
      "../pittsburgh-west.osm",
    }) osm.AddFile(osmfile);

  printf("Loaded OSM with %lld points, %lld ways\n",
         osm.nodes.size(), osm.ways.size());

  int64 pts = 0;
  for (auto &p : pactom->paths) pts += p.size();

  printf("Loaded %lld paths with %lld waypoints.\n",
         pactom->paths.size(), pts);
  printf("There are %d hoods\n", pactom->hoods.size());

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

  Bounds::Scaler scaler = bounds.ScaleToFit(WIDTH * SCALE,
                                            HEIGHT * SCALE).FlipY();

  ImageRGBA image(WIDTH * SCALE, HEIGHT * SCALE);
  image.Clear32(0x000000FF);

  for (const auto &[way_id, way] : osm.ways) {
    if (DrawRoad(way.highway)) {
      const uint32 color = WayColor(way.highway);
      for (int i = 0; i < way.points.size() - 1; i++) {
        const uint64_t id0 = way.points[i];
        const uint64_t id1 = way.points[i + 1];

        auto it0 = osm.nodes.find(id0);
        auto it1 = osm.nodes.find(id1);
        if (it0 != osm.nodes.end() &&
            it1 != osm.nodes.end()) {
          const LatLon latlon0 = it0->second;
          const LatLon latlon1 = it1->second;
          auto [x0, y0] = scaler.Scale(Project(latlon0));
          auto [x1, y1] = scaler.Scale(Project(latlon1));

          DrawThickLine<RADIUS>(&image, x0, y0, x1, y1, color);
        }
      }
    }
  }

  for (const auto &[name, path] : pactom->hoods) {
    constexpr uint32 color = 0x909090FF;
    for (int i = 0; i < path.size() - 1; i++) {
      const LatLon latlon0 = path[i];
      const LatLon latlon1 = path[i + 1];
      auto [x0, y0] = scaler.Scale(Project(latlon0));
      auto [x1, y1] = scaler.Scale(Project(latlon1));

      DrawThickLine<RADIUS>(&image, x0, y0, x1, y1, color);
    }
  }

  #if 0
  for (const auto &p : pactom->paths) {
    const uint32 color = RandomBrightColor(&rc) & 0xFFFFFF33; // XXX
    for (int i = 0; i < p.size() - 1; i++) {
      const auto &[latlon0, elev0] = p[i];
      const auto &[latlon1, elev1] = p[i + 1];
      auto [x0, y0] = scaler.Scale(Project(latlon0));
      auto [x1, y1] = scaler.Scale(Project(latlon1));

      DrawThickLine<RADIUS>(&image, x0, y0, x1, y1, color);
    }
  }
  #endif

  ImageRGBA out = image.ScaleDownBy(SCALE);

  out.Save("maptest.png");
  printf("Wrote maptest.png.\n");

  return 0;
}
