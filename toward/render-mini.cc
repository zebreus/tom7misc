
#include <algorithm>
#include <format>
#include <string>
#include <optional>
#include <string_view>
#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "cell-library.h"
#include "circuit.h"
#include "image.h"
#include "layout.h"
#include "png.h"
#include "render-circuit.h"
#include "util.h"

static constexpr int MAX_LAYERS = 1000;
static void RenderTop(const Layout &layout, std::string_view outfile) {
  CellLibrary library;
  Circuit circuit;
  size_t n_layers = std::min(layout.circuit.layers.size(),
                             (size_t)MAX_LAYERS);
  circuit.layers.reserve(n_layers);
  for (size_t i = 0; i < n_layers; i++) {
    circuit.layers.push_back(layout.circuit.layers[i]);
  }

  std::optional<int> min_left;
  std::optional<int> min_right;

  for (const auto &layer : circuit.layers) {
    if (layer.empty()) continue;

    int left = (layer.front().gate == Gate::SPACER) ? layer.front().v : 0;
    if (!min_left.has_value() || left < *min_left) {
      min_left = left;
    }

    int right = 0;
    if (layer.size() > 1 && layer.back().gate == Gate::SPACER) {
      right = layer.back().v;
    }
    if (!min_right.has_value() || right < *min_right) {
      min_right = right;
    }
  }

  if (min_left.value_or(0) > 0) {
    for (auto &layer : circuit.layers) {
      if (layer.empty()) continue;
      if (layer.front().gate == Gate::SPACER) {
        layer.front().v -= *min_left;
        if (layer.front().v <= 0) {
          layer.erase(layer.begin());
        }
      }
    }
  }

  if (min_right.value_or(0) > 0) {
    for (auto &layer : circuit.layers) {
      if (layer.empty()) continue;
      if (layer.back().gate == Gate::SPACER) {
        layer.back().v -= *min_right;
        if (layer.back().v <= 0) {
          layer.pop_back();
        }
      }
    }
  }

  std::vector<bool> is_vars(layout.input_vars.size(), true);
  ImageRGBA img = RenderCircuitMini(library, circuit, is_vars);

  std::vector<uint8_t> png = PNG::EncodeInMemory(img);
  Util::WriteFileBytes(outfile, png);
  Print("Wrote " AGREEN("{}") ".\n", outfile);
}


int main(int argc, char **argv) {
  ANSI::Init();
  CHECK(argc == 2) << "./render-mini file.layout\n";
  std::string_view file = argv[1];

  std::optional<Layout> olay = LayoutEngine::Parse(Util::ReadFile(file));
  CHECK(olay.has_value()) << file;

  std::string outfile = std::format("{}-top.png", Util::FileBaseOf(file));
  RenderTop(olay.value(), outfile);

  return 0;
}
