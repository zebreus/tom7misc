
#include "pactom.h"

#include <string>
#include <vector>
#include <memory>
#include <cstdint>

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

using namespace std;


using int64 = int64_t;

static constexpr int WIDTH = 1920;
static constexpr int HEIGHT = 1080;
static constexpr int SCALE = 4;
// Additional pixels to draw for line (0 = 1 pixel thick)
static constexpr int RADIUS = 3;
// circle at end while in motino
static constexpr int DOT_RADIUS = 16;

static constexpr int NUM_FRAMES = 256;

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

int main(int argc, char **argv) {
  ArcFour rc("pactom");
  unique_ptr<PacTom> pactom = PacTom::FromFiles({"../pac.kml",
                                                 "../pac2.kml"},
    "../neighborhoods.kml"
    );
  CHECK(pactom.get() != nullptr);

  std::vector<uint32_t> colors;
  std::vector<std::pair<uint32_t,
                        std::vector<std::pair<LatLon, double>>>> paths;
  for (const auto &r : pactom->runs) {
    uint32_t color = RandomBrightColor(&rc) & 0xFFFFFF33; // XXX
    colors.emplace_back(color);
  }

  const LatLon home = LatLon::FromDegs(40.452911, -79.936313);
  LatLon::Projection Project = LatLon::Gnomonic(home);
  const std::pair<double, double> home_pt = Project(home);

  // Find the extrema.
  Bounds bounds;
  for (const auto &r : pactom->runs) {
    for (const auto &[latlon, elev] : r.path) {
      auto [x, y] = Project(latlon);
      bounds.Bound(x, y);
    }
  }
  bounds.AddMarginFrac(0.05);

  // The bounds for the full data are the endpoint.
  Bounds::Scaler scaler_end = bounds.ScaleToFit(WIDTH * SCALE,
                                                HEIGHT * SCALE).FlipY();

  // Screen location of home.
  auto home_screen = scaler_end.Scale(home_pt);

  // Start scale; centered on home.
  Bounds::Scaler scaler_start = scaler_end.
    // put home at 0,0
    PanScreen(-home_screen.first, -home_screen.second).
    // zoom
    Zoom(6.0, 6.0).
    // center screen on 0, 0
    PanScreen(WIDTH * SCALE * 0.5, HEIGHT * SCALE * 0.5);

  auto ScaleInterp = [&](double f, std::pair<double, double> pt) ->
    std::pair<double, double> {
      auto [x0, y0] = scaler_start.Scale(pt);
      auto [x1, y1] = scaler_end.Scale(pt);

      return std::make_pair(f * x1 + (1.0 - f) * x0,
                            f * y1 + (1.0 - f) * y0);
    };

  auto MakeFrame = [&](int64_t frame) {
      const double frame_frac = frame / (double)(NUM_FRAMES - 1);

      ImageRGBA image(WIDTH * SCALE, HEIGHT * SCALE);
      image.Clear32(0x000000FF);

      for (const auto &[name, path] : pactom->hoods) {
        constexpr uint32 color = 0x909090FF;
        for (int i = 0; i < path.size() - 1; i++) {
          const LatLon latlon0 = path[i];
          const LatLon latlon1 = path[i + 1];
          auto [x0, y0] = ScaleInterp(frame_frac, Project(latlon0));
          auto [x1, y1] = ScaleInterp(frame_frac, Project(latlon1));

          DrawThickLine<RADIUS>(&image, x0, y0, x1, y1, color);
        }
      }

      for (int idx = 0; idx < pactom->runs.size(); idx++) {
        const uint32_t color = colors[idx];
        const auto &p = pactom->runs[idx].path;
        const int last_pt =
          std::clamp((int)std::round(p.size() * frame_frac), 0, (int)p.size());
        for (int i = 0; i < last_pt - 1; i++) {
          const auto &[latlon0, elev0] = p[i];
          const auto &[latlon1, elev1] = p[i + 1];
          auto [x0, y0] = ScaleInterp(frame_frac, Project(latlon0));
          auto [x1, y1] = ScaleInterp(frame_frac, Project(latlon1));

          DrawThickLine<RADIUS>(&image, x0, y0, x1, y1, color);

          if (i == last_pt - 2 && last_pt != p.size()) {
            uint32_t dot_color = color | 0x77;
            image.BlendFilledCircle32(x1, y1, DOT_RADIUS, dot_color);
          }
        }
      }

      ImageRGBA out = image.ScaleDownBy(SCALE);

      out.Save(StringPrintf("zoom%04d.png", (int)frame));
      printf(".");
    };

  ParallelComp(NUM_FRAMES, MakeFrame, 8);

  return 0;
}
