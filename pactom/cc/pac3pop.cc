
#include "pactom.h"

#include <unordered_set>
#include <algorithm>
#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <map>

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

static constexpr int WIDTH = 1920;
static constexpr int HEIGHT = 1080;
static constexpr int SCALE = 4;
// Additional pixels to draw for line (0 = 1 pixel thick)
static constexpr int RADIUS = 4;

using Run = PacTom::Run;

static constexpr uint32_t OLD_COLOR = 0xFF3333AA;
static constexpr uint32_t NEW_COLOR = 0xFFFF33AA;

static constexpr int ZOOM_FRAMES = 4 * 60;

static double EaseInOutSin(double f) {
  return 0.5 * (1.0 + sin(3.141592653589 * (f - 0.5)));
}

int main(int argc, char **argv) {
  AnsiInit();

  ArcFour rc("pactom");
  // Need to load pac3 too!
  // unique_ptr<PacTom> pactom = PacTomUtil::Load(false);
  unique_ptr<PacTom> pactom = PacTom::FromFiles({"../pac.kml",
                                                 "../pac2.kml",
                                                 "../pac3.kml"},
    "../neighborhoods.kml",
    "../allegheny-county.kml",
    false);

  CHECK(pactom.get() != nullptr);

  const LatLon home = LatLon::FromDegs(40.452911, -79.936313);
  LatLon::Projection Project = LatLon::Gnomonic(home);
  LatLon::InverseProjection InverseProj = LatLon::InverseGnomonic(home);

  Bounds pgh_bounds;
  for (const auto &[name, path] : pactom->hoods) {
    for (const auto &ll : path) {
      auto [x, y] = Project(ll);
      pgh_bounds.Bound(x, y);
    }
  }
  pgh_bounds.AddMarginFrac(0.05);

  Bounds allegheny_bounds;
  for (const auto &[name, path] : pactom->munis) {
    for (const auto &ll : path) {
      auto [x, y] = Project(ll);
      allegheny_bounds.Bound(x, y);
    }
  }
  allegheny_bounds.AddMarginFrac(0.05);

  Bounds::Scaler scaler_start =
    pgh_bounds.ScaleToFit(WIDTH * SCALE, HEIGHT * SCALE).FlipY();
  Bounds::Scaler scaler_end =
    allegheny_bounds.ScaleToFit(WIDTH * SCALE, HEIGHT * SCALE).FlipY();

#if 0
  auto ScaleInterp = [&](double f, std::pair<double, double> pt) ->
    std::pair<double, double> {
    auto [x0, y0] = scaler_start.Scale(pt);
    auto [x1, y1] = scaler_end.Scale(pt);

    return std::make_pair(f * x1 + (1.0 - f) * x0,
                          f * y1 + (1.0 - f) * y0);
  };
#endif

  auto IsPac3 = [](const Run &run) {
      return run.from_file == "../pac3.kml";
    };

  auto MakeColors = [&rc](
      const std::map<std::string, std::vector<LatLon>> &hoods) {
      std::vector<uint32_t> ret;
      ret.reserve(hoods.size());
      for (const auto &[name, path] : hoods) {
        ret.push_back(PacTomUtil::RandomBrightColor(&rc));
      }
      return ret;
    };

  std::vector<uint32_t> hood_colors = MakeColors(pactom->hoods);
  std::vector<uint32_t> muni_colors = MakeColors(pactom->munis);

  for (uint32_t &c : hood_colors) {
    const auto [r, g, b, a_] = ColorUtil::U32ToFloats(c);
    const auto [mr, mg, mb] = ColorUtil::Mix3Channels(
        r, g, b,
        0.0f, 0.25f, 0.15f,
        0.50f);
    c = ColorUtil::FloatsTo32(mr, mg, mb, 1.0f);
  }
  for (uint32_t &c : muni_colors) {
    const auto [r, g, b, a_] = ColorUtil::U32ToFloats(c);
    const auto [mr, mg, mb] = ColorUtil::Mix3Channels(
        r, g, b,
        0.00f, 0.0f, 0.33f,
        0.75f);
    c = ColorUtil::FloatsTo32(mr, mg, mb, 1.0f);
  }

  auto DrawScreen = [&](
      ImageRGBA *image,
      const Bounds::Scaler &scaler) {
      for (int y = 0; y < image->Height(); y++) {
        for (int x = 0; x < image->Width(); x++) {
          const auto [xx, yy] = scaler.Unscale(x, y);
          const LatLon pos = InverseProj(xx, yy);
          const int hood = pactom->InNeighborhood(pos);
          if (hood != -1) {
            image->SetPixel32(x, y, hood_colors[hood]);
          } else {
            const int muni = pactom->InMuni(pos);
            if (muni != -1) {
              image->SetPixel32(x, y, muni_colors[muni]);
            }
          }
        }
      }
    };

  auto DrawRun = [&](
      ImageRGBA *image,
      const Bounds::Scaler &scaler,
      const Run &run,
      uint32_t color) {
      for (int i = 0; i < run.path.size() - 1; i++) {
        const LatLon latlon0 = run.path[i].first;
        const LatLon latlon1 = run.path[i + 1].first;
        auto [x0, y0] = scaler.Scale(Project(latlon0));
        auto [x1, y1] = scaler.Scale(Project(latlon1));

        PacTomUtil::DrawThickLine<RADIUS>(image, x0, y0, x1, y1, color);
      }
    };

  // Zoom portion
  ParallelComp(
      ZOOM_FRAMES,
      [&](int idx) {
        ImageRGBA image(WIDTH * SCALE, HEIGHT * SCALE);
        image.Clear32(0x000000FF);

        // XXX ease in/out
        double t_linear = idx / (double)(ZOOM_FRAMES - 1);
        double t = EaseInOutSin(t_linear);

        Bounds bounds = pgh_bounds;

        // Would be nice to add some interpolation to pairs of
        // bounds/scalers for this kind of thing.
        {
          double x0 = std::lerp(
              pgh_bounds.MinX(), allegheny_bounds.MinX(), t);
          double y0 = std::lerp(
              pgh_bounds.MinY(), allegheny_bounds.MinY(), t);
          double x1 = std::lerp(
              pgh_bounds.MaxX(), allegheny_bounds.MaxX(), t);
          double y1 = std::lerp(
              pgh_bounds.MaxY(), allegheny_bounds.MaxY(), t);
          bounds.Bound(x0, y0);
          bounds.Bound(x1, y1);
        }

        Bounds::Scaler scaler =
          bounds.ScaleToFit(WIDTH * SCALE, HEIGHT * SCALE).FlipY();

        DrawScreen(&image, scaler);

        for (const Run &run : pactom->runs)
          if (!IsPac3(run))
            DrawRun(&image, scaler, run, OLD_COLOR);

        ImageRGBA out = image.ScaleDownBy(SCALE);

        string filename = StringPrintf("pac3/zoom%d.png", idx);
        out.Save(filename);
        printf("Wrote %s.\n", filename.c_str());
      },
      12);

  std::vector<Run> pac3;
  for (const Run &run : pactom->runs) {
    if (IsPac3(run)) {
      pac3.push_back(run);
    }
  }

  printf("%d pac3 runs\n", (int)pac3.size());

  Shuffle(&rc, &pac3);

  std::unordered_set<std::string> goes_first, goes_last;

  goes_first.insert("Fox Pachel 6-13-2020");
  goes_first.insert("Oakmont via ARB");

  goes_last.insert("Dark Hollow Woods 4/23/2021");
  goes_last.insert("O'hilly O'hara 3/11/2022");
  goes_last.insert("Solo marathon to South Park 8-15-2020");
  goes_last.insert("Frigid 5 Mile");

  std::sort(pac3.begin(), pac3.end(),
            [&](const Run &a, const Run &b) {
              if (goes_first.contains(a.name) &&
                  goes_first.contains(b.name))
                return false;
              if (goes_first.contains(a.name)) return true;
              if (goes_first.contains(b.name)) return false;

              if (goes_last.contains(a.name) &&
                  goes_last.contains(b.name))
                return false;
              if (goes_last.contains(a.name)) return false;
              if (goes_last.contains(b.name)) return true;

              return false;
            });

  printf("In this order:\n");
  for (int i = 0; i < pac3.size(); i++) {
    printf("  %d. %s\n", i, pac3[i].name.c_str());
  }

  ParallelComp(
      pac3.size() + 1,
      [&](int idx) {
        ImageRGBA image(WIDTH * SCALE, HEIGHT * SCALE);
        image.Clear32(0x000000FF);

        Bounds::Scaler scaler = allegheny_bounds.
          ScaleToFit(WIDTH * SCALE, HEIGHT * SCALE).FlipY();

        DrawScreen(&image, scaler);

        // All old runs.
        for (const Run &run : pactom->runs)
          if (!IsPac3(run))
            DrawRun(&image, scaler, run, OLD_COLOR);

        for (int i = 0; i < idx; i++) {
          if (i < pac3.size()) {
            DrawRun(&image, scaler, pac3[i], NEW_COLOR);
          }
        }

        ImageRGBA out = image.ScaleDownBy(SCALE);

        string filename = StringPrintf("pac3/pop%d.png", idx);
        out.Save(filename);
        printf("Wrote %s.\n", filename.c_str());
      },
      12);

  return 0;
}
