
#include "pactom.h"

#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <unordered_set>
#include <unordered_map>

#include "base/logging.h"
#include "base/stringprintf.h"
#include "geom/latlon.h"
#include "bounds.h"
#include "image.h"
#include "lines.h"
#include "arcfour.h"
#include "randutil.h"
#include "color-util.h"
#include "osm.h"
#include "ansi.h"
#include "threadutil.h"

#include "pactom-util.h"

using namespace std;


using int64 = int64_t;

static constexpr double METERS_TO_FEET = 3.28084;
static constexpr int WIDTH = 1920;
static constexpr int HEIGHT = 1080;
static constexpr int SCALE = 4;
// Additional pixels to draw for line (0 = 1 pixel thick)
static constexpr int RADIUS = 2;

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

  unique_ptr<PacTom> pactom;
  OSM osm;

  InParallel([&]() {
      pactom = PacTomUtil::Load(false);
      CHECK(pactom.get() != nullptr);
    },
  [&]() {
    for (const string osmfile : {
        "../pittsburgh-center.osm",
          "../pittsburgh-northeast.osm",
          "../pittsburgh-north.osm",
          "../pittsburgh-south.osm",
          "../pittsburgh-southwest.osm",
          "../pittsburgh-west.osm",
          }) osm.AddFile(osmfile);
  });

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
  LatLon::InverseProjection InverseProj = LatLon::InverseGnomonic(home);

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

  std::unordered_map<int, uint32_t> color;

  std::vector<int> hoods;
  for (int i = 0; i < pactom->hoods.size(); i++) {
    hoods.push_back(i);
    color[i] = PacTomUtil::RandomBrightColor(&rc);
  }
  Shuffle(&rc, &hoods);

  std::unordered_set<int> done;

  Asynchronously async(8);
  for (int img_idx = 0; true; img_idx++) {

    if (img_idx == 93) {
    ImageRGBA image(WIDTH * SCALE, HEIGHT * SCALE);
    image.Clear32(0x000000FF);

    for (int y = 0; y < image.Height(); y++) {
      for (int x = 0; x < image.Width(); x++) {
        const auto [xx, yy] = scaler.Unscale(x, y);
        const LatLon pos = InverseProj(xx, yy);
        const int hood = pactom->InNeighborhood(pos);
        if (hood != -1) {
          uint32_t c = color[hood];
          if (done.contains(hood)) {
            auto [r, g, b, a] = ColorUtil::U32ToFloats(c);
            uint32_t darkc =
              ColorUtil::FloatsTo32(r * 0.25f, g * 0.25f, b * 0.25f, 1.0f);
            image.SetPixel32(x, y, darkc);
          } else {
            image.SetPixel32(x, y, c);
          }
        }
      }
    }

    async.Run([img_idx, image = std::move(image)]() {
        ImageRGBA out = image.ScaleDownBy(SCALE);
        string filename = StringPrintf("animhood/animhood-%d.png", img_idx);
        out.Save(filename);
        printf("Wrote %s\n", filename.c_str());
      });
    }

    if (hoods.empty())
      break;

    done.insert(hoods.back());
    hoods.pop_back();
  }


  return 0;
}
