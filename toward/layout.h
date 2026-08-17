
// Take propositions and encode them as a circuit.

#ifndef _TOWARD_LAYOUT_H
#define _TOWARD_LAYOUT_H

#include <deque>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "cell-library.h"
#include "circuit.h"
#include "layout-canvas.h"
#include "prop.h"

struct StatusBar;

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

  // If something goes wrong (like we reach the maximum depth or
  // we get in a cycle) this may return an error message as the
  // string component of the variant.
  virtual std::variant<Layout, std::string> DoLayoutExt(
      std::span<const Prop> props, std::optional<int> max_layers = {}) = 0;

  virtual void SetVerbose(int v) = 0;
  virtual void SetWriteImages(bool yes) = 0;
  virtual void SetWriteDebugging(bool yes) = 0;

  // This stuff is just exposed for testing and visualization.

  // Debugging info.
  // The front of the deque is the topmost layer.
  struct SpringRecord {
    int start_pos = 0;
    float ideal_pos = 0.0f;
    int target_dist = 0;
    int min_dist = 0;
    float compress = 0.0f;
    float expand = 0.0f;
    bool anchored = false;
  };

  using LC = LayoutCanvas::LC;
  using Chute = LayoutCanvas::Chute;
  using PC = LayoutCanvas::PC;

  virtual std::tuple<std::vector<LC>, int, std::vector<SpringRecord>>
  AddLayer(const std::deque<std::vector<LC>> &layers,
           const std::unordered_map<Prop, int> &prop_ranks,
           bool allow_placement = true) = 0;

  virtual std::optional<std::vector<std::pair<int, CType>>>
  AllVars(std::span<const LC> lcs) = 0;

  virtual std::unordered_map<Prop, int>
  GetPropRanks(std::span<const LC> layer,
               std::string_view debug_filename = "") = 0;

  // Add a layer to the top. The input may not be empty, and must
  // not be done (all variables).
  virtual void DoAddLayer(std::deque<std::vector<LC>> *layers,
                          const std::unordered_map<Prop, int> &prop_ranks) = 0;

  // Advanced!
  // Must have 3 lines.
  virtual void SetStatusBar(StatusBar *s) = 0;

  virtual const std::deque<std::vector<SpringRecord>> &GetSpringHistory()
    const = 0;

  // Pretty-print for debugging, etc. Designed for small
  // circuits!
  static std::string ToString(const Layout &layout);

  // Number of redundant inputs (same var appears earlier,
  // including of a different type).
  static int RedundantInputs(const Layout &layout);

  // Single line of stats: Size, number of inputs, etc.
  static std::string LayoutInfo(const Layout &layout);

  static std::vector<uint8_t> Serialize(const Layout &layout);
  static std::optional<Layout> Parse(std::string_view content);
  static std::optional<Layout> Parse(std::span<const uint8_t> content);

  static Layout Normalize(Layout layout);


  // For externally-driven optimization. Not recommended to
  // change these.

  virtual void SetExtWeight(float w) = 0;
  virtual void SetAdditionalAdditionalClearance(int a) = 0;
  virtual void SetClearanceCompressionWeight(float c) = 0;
  virtual void SetCorrectSpringWeight(float w) = 0;
  virtual void SetQuiesceDistance(int d) = 0;
  virtual void SetAdditionalMinQuiesceDistance(int a) = 0;
  virtual void SetQuiesceCompress(float f) = 0;
  virtual void SetQuiesceExpand(float f) = 0;

 protected:
  LayoutEngine();
};



#endif
