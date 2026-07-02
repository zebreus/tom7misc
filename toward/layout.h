
// Take propositions and encode them as a circuit.

#ifndef _TOWARD_LAYOUT_H
#define _TOWARD_LAYOUT_H

#include <deque>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "cell-library.h"
#include "circuit.h"
#include "layout-canvas.h"
#include "prop.h"

struct Layout {
  // The topmost layer needs variables as inputs.
  // A future version might guarantee that CType is mixed.
  // This will match the input arity of the first layer.
  std::vector<std::pair<int, CType>> input_vars;
  Circuit circuit;
};

struct LayoutEngine {
  static std::unique_ptr<LayoutEngine> Create(const CellLibrary &library,
                                              const World &world);
  virtual ~LayoutEngine();

  // Props must all be in the same world.
  virtual Layout DoLayout(std::span<const Prop> props) = 0;

  virtual void SetVerbose(int v) = 0;

  // This stuff is just exposed for testing and visualization.

  using LC = LayoutCanvas::LC;
  using Chute = LayoutCanvas::Chute;
  using PC = LayoutCanvas::PC;

  virtual std::pair<std::vector<LC>, int>
  AddLayer(std::span<const LC> top) = 0;

  virtual std::optional<std::vector<std::pair<int, CType>>>
  AllVars(std::span<const LC> lcs) = 0;

  // Add a layer to the top. The input may not be empty, and must
  // not be done (all variables).
  virtual void DoAddLayer(std::deque<std::vector<LC>> *layers) = 0;

 protected:
  LayoutEngine();
};

#endif
