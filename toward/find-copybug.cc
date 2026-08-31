
#include <algorithm>
#include <ctime>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ansi.h"
#include "base/print.h"
#include "cell-library.h"
#include "circuit-sim.h"
#include "circuit.h"
#include "drc.h"
#include "layout-reducer.h"
#include "layout.h"
#include "opt/opt.h"
#include "rendering.h"
#include "scene.h"
#include "util.h"

Layout ExtractCopiedLayout(const CellLibrary &library, const Layout &layout,
                           vec2f aabb_min, vec2f aabb_max) {
  CircuitSim sim(library, nullptr, layout);
  return sim.ExtractOverlapping(aabb_min, aabb_max);
}

void PrintMinimalExample(const CellLibrary &library,
                         const Layout &layout,
                         vec2f aabb_min, vec2f aabb_max) {
  std::string filename = std::format("layout-bug-{}.layout",
                                     time(nullptr));
  Util::WriteFileBytes(filename, LayoutEngine::Serialize(layout));

  Print("Wrote {}. AABB: ({:.11g},{:.11g}) ({:.11g},{:.11g})\n",
        filename, aabb_min.x, aabb_min.y, aabb_max.x, aabb_max.y);

  Print("Layout:\n{}\n", LayoutEngine::ToString(layout));

  Layout copied = ExtractCopiedLayout(library, layout, aabb_min, aabb_max);
  Print("Copied layout:\n{}\n", LayoutEngine::ToString(copied));

  auto RenderToDisk = [&](const std::string &base_filename,
                          const Layout &lay,
                          bool add_overlay) {
    std::unique_ptr<Rendering> rendering = CreateImageRendering(base_filename);
    CircuitSim sim(library, rendering.get(), lay);

    sim.ZoomToFit();

    std::vector<Rendering::Triangle> tris;
    sim.FillVisibleTriangles(&tris);

    if (add_overlay) {
      uint32_t color = 0x3333AA55;
      tris.push_back({
          .a = {aabb_min.x, aabb_min.y},
          .b = {aabb_max.x, aabb_min.y},
          .c = {aabb_max.x, aabb_max.y},
          .rgba = color, .reserved = 0});
      tris.push_back({
          .a = {aabb_min.x, aabb_min.y},
          .b = {aabb_max.x, aabb_max.y},
          .c = {aabb_min.x, aabb_max.y},
          .rgba = color, .reserved = 0});
    }

    rendering->RenderScene(sim.ViewPos(), sim.ViewPosMax(), tris);
  };

  RenderToDisk("bug-source", layout, true);
  RenderToDisk("bug-copy", copied, false);

  std::optional<std::string> err = DRC::GetLayoutError(
      library, "copied", copied);
  CHECK(err.has_value()) << "No bug?";
  Print("\nDRC error:\n{}\n", err.value());
}

void FindMinimalExample(const Layout &original) {
  CellLibrary library;
  std::optional<std::string> original_err = DRC::GetLayoutError(
      library, "original", original);
  CHECK(!original_err.has_value()) << "Initial layout is broken? "
                                   << original_err.value();

  vec2f best_min = {0.0f, 0.0f};
  vec2f best_max = {0.0f, 0.0f};

  auto Pred = [&library, &best_min, &best_max](const Layout &layout) {
    CircuitSim sim(library, nullptr, layout);
    sim.ZoomToFit();
    vec2f view_max = sim.ViewPosMax();
    float max_x = view_max.x;
    float max_y = view_max.y;

    auto f = [&library, &sim](double x1, double x2,
                              double y1, double y2) -> double {
      vec2f aabb_min = {(float)std::min(x1, x2), (float)std::min(y1, y2)};
      vec2f aabb_max = {(float)std::max(x1, x2), (float)std::max(y1, y2)};

      Layout ext = sim.ExtractOverlapping(aabb_min, aabb_max);
      size_t sz = CircuitSize(ext.circuit);
      if (sz == 0) {
        return 1e9;
      }

      if (DRC::GetLayoutError(library, "ext", ext).has_value()) {
        float dx = aabb_max.x - aabb_min.x;
        float dy = aabb_max.y - aabb_min.y;
        // Primary metric is the size of the copied circuit, but
        // also have some gradient towards smaller bounding boxes.
        return sz * 1000 + (dx + dy);
      }
      return 1e9;
    };

    auto [aarg, best_v] = Opt::Minimize4D(
        f,
        {0.0, 0.0, 0.0, 0.0},
        {(double)max_x, (double)max_x, (double)max_y, (double)max_y},
        1000);

    if (best_v < 1e9) {
      auto [x1, x2, y1, y2] = aarg;
      best_min = {(float)std::min(x1, x2), (float)std::min(y1, y2)};
      best_max = {(float)std::max(x1, x2), (float)std::max(y1, y2)};
      return true;
    }
    return false;
  };

  if (!Pred(original)) {
    Print("Original layout does not exhibit the bug (or random search "
          "missed it).\n");
    return;
  }

  std::unique_ptr<Reducer> reducer = Reducer::Create(library);
  Layout result = reducer->ReduceWhile(original, 1000, Pred);

  // Ensure best_min and best_max correspond to the final reduced layout.
  CHECK(Pred(result)) << "Bug disappeared from final result?";

  PrintMinimalExample(library, result, best_min, best_max);
}

int main(int argc, char **argv) {
  ANSI::Init();
  CHECK(argc == 2) << "Usage: ./find-copybug.exe input.layout\n";

  std::optional<Layout> input =
    LayoutEngine::Parse(Util::ReadFileBytes(argv[1]));
  CHECK(input.has_value()) << argv[1];
  FindMinimalExample(input.value());

  return 0;
}
