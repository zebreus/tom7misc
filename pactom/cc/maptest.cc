
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
#include "ansi.h"

using namespace std;


using int64 = int64_t;

static constexpr double METERS_TO_FEET = 3.28084;
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
  AnsiInit();

  ArcFour rc("pactom");
  unique_ptr<PacTom> pactom = PacTom::FromFiles({"../pac.kml",
                                                 "../pac2.kml"},
    "../neighborhoods.kml"
    );
  CHECK(pactom.get() != nullptr);

  int has_date = 0;
  double path_feet = 0.0, tripath_feet = 0.0;
  for (int ridx = 0; ridx < pactom->runs.size(); ridx++) {
    const auto &run = pactom->runs[ridx];
    double pf = 0.0, tf = 0.0;
    for (int i = 0; i < run.path.size() - 1; i++) {
      const auto &[latlon0, elev0] = run.path[i];
      const auto &[latlon1, elev1] = run.path[i + 1];
      double dist1 = LatLon::DistFeet(latlon0, latlon1);
      pf += dist1;
      double dz = (elev1 - elev0) * METERS_TO_FEET;

      tf += sqrt(dz * dz + dist1 * dist1);
    }

    const char *DCOLOR = run.year == 0 ? ANSI_RED : ANSI_PURPLE;
    if (run.year > 0) has_date++;
    printf("%d" AGREY(".") " "
           "%s%04d" ANSI_RESET "-"
           "%s%02d" ANSI_RESET "-"
           "%s%02d" ANSI_RESET " "
           ABLUE("%s") AGREY(":") " "
           AYELLOW("%.3f") AGREY("/") AWHITE("%.3f") " "
           "mi.\n", ridx,
           DCOLOR, run.year, DCOLOR, run.month, DCOLOR, run.day,
           run.name.c_str(),
           pf / 5280.0, tf / 5280.0);

    path_feet += pf;
    tripath_feet += tf;
  }
  printf("Total miles: %.6f\n", path_feet / 5280.0);
  printf("Including elev: %.6f\n", tripath_feet / 5280.0);
  printf("%d/%d have dates\n", has_date, pactom->runs.size());

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
  for (const auto &r : pactom->runs)
    pts += r.path.size();

  printf("Loaded %lld runs with %lld waypoints.\n",
         pactom->runs.size(), pts);
  printf("There are %d hoods\n", pactom->hoods.size());

  const LatLon home = LatLon::FromDegs(40.452911, -79.936313);
  LatLon::Projection Project = LatLon::Gnomonic(home);
  // LatLon::Projection Project = LatLon::PlateCarree();

  // Find the extrema.
  Bounds bounds;
  for (const auto &r : pactom->runs) {
    for (const auto &[latlon, elev] : r.path) {
      auto [x, y] = Project(latlon);
      bounds.Bound(x, y);
    }
  }
  bounds.AddMarginFrac(0.05);

  Bounds::Scaler scaler = bounds.ScaleToFit(WIDTH * SCALE,
                                            HEIGHT * SCALE).FlipY();

  ImageRGBA image(WIDTH * SCALE, HEIGHT * SCALE);
  image.Clear32(0x000000FF);

  double road_feet = 0.0;
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

          if (-1 != pactom->InNeighborhood(latlon0) &&
              -1 != pactom->InNeighborhood(latlon1)) {

            road_feet += LatLon::DistFeet(latlon0, latlon1);
            DrawThickLine<RADIUS>(&image, x0, y0, x1, y1, color);
          } else {
            DrawThickLine<RADIUS>(&image, x0, y0, x1, y1, 0x000033FF);
          }
        }
      }
    }
  }
  printf("Total road miles: %.6f\n", road_feet / 5280.0);

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

  for (const auto &r : pactom->runs) {
    const uint32 color = RandomBrightColor(&rc) & 0xFFFFFF33; // XXX
    for (int i = 0; i < r.path.size() - 1; i++) {
      const auto &[latlon0, elev0] = r.path[i];
      const auto &[latlon1, elev1] = r.path[i + 1];
      auto [x0, y0] = scaler.Scale(Project(latlon0));
      auto [x1, y1] = scaler.Scale(Project(latlon1));

      DrawThickLine<RADIUS>(&image, x0, y0, x1, y1, color);
    }
  }

  ImageRGBA out = image.ScaleDownBy(SCALE);

  out.Save("maptest.png");
  printf("Wrote maptest.png.\n");

  return 0;
}
