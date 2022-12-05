
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
#include "timer.h"

#include "pactom-util.h"

using namespace std;


using int64 = int64_t;

static constexpr double PI = std::numbers::pi;
static constexpr double METERS_TO_FEET = 3.28084;
static constexpr int WIDTH = 1920 * 4;
static constexpr int HEIGHT = 1080 * 4;
static constexpr int SCALE = 4;
// Additional pixels to draw for line (0 = 1 pixel thick)
static constexpr int RADIUS = 2 * 4;

using SpanningTree = PacTom::SpanningTree;

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

struct Shortest {

};

int main(int argc, char **argv) {
  AnsiInit();

  ArcFour rc("pactom");
  unique_ptr<PacTom> pactom = PacTomUtil::Load(false);
  CHECK(pactom.get() != nullptr);

  const LatLon home = LatLon::FromDegs(40.452911, -79.936313);
  LatLon::Projection Project = LatLon::Gnomonic(home);

  Timer stree_timer;
  SpanningTree stree = pactom->MakeSpanningTree(home);
  printf("Made spanning tree in %.3f sec\n", stree_timer.Seconds());

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

  for (const SpanningTree::Node &node : stree.nodes) {
    if (node.parent >= 0) {
      const SpanningTree::Node &other = stree.nodes[node.parent];
      auto [x0, y0] = scaler.Scale(Project(node.pos));
      auto [x1, y1] = scaler.Scale(Project(other.pos));
      const optional<double> rhoo = LatLon::Angle(node.pos, other.pos);
      if (!rhoo.has_value()) continue;
      auto [r, g, b] = ColorUtil::HSVToRGB(
          rhoo.value() / (PI * 2.0), 1.0, 1.0);

      const uint32_t color = ColorUtil::FloatsTo32(r, g, b, 0.25f);
      DrawThickLine<RADIUS>(&image, x0, y0, x1, y1, color);
    }
  }

  ImageRGBA out = image.ScaleDownBy(SCALE);

  out.Save("shortest.png");
  printf("Wrote shortest.png.\n");

  return 0;
}
