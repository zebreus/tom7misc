
// Parser for a simple subset of SVG. Incomplete!
//
// Tips for Illustrator export (CC 2026): Use "presentation
// attributes" or "style attributes". Both "Save as..." and "Export
// as..." seem to work fine. Note that Illustrator will usually
// flatten objects that have both fill and stroke into two fills.

#ifndef _CC_LIB_SVG_H
#define _CC_LIB_SVG_H

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

struct SVG {

  // Use Parse() to get an SVG Document (see below).

  // Path commands.
  struct MoveTo {
    double x, y;
  };

  struct LineTo {
    double x, y;
  };

  // Note: Quadratic Bézier (one control point) curves are
  // converted into the equivalent Cubic.
  struct CubicBezier {
    double cx1, cy1;
    double cx2, cy2;
    double x, y;
  };

  struct ClosePath {
  };

  using PathCommand = std::variant<MoveTo, LineTo, CubicBezier, ClosePath>;

  enum class LineCap : uint8_t {
    BUTT,
    ROUND,
    SQUARE,
  };

  enum class LineJoin : uint8_t {
    ARCS,
    BEVEL,
    MITER,
    MITER_CLIP,
    ROUND,
  };

  static constexpr uint32_t COLOR_NONE = 0x00000000;

  struct Style {
    // Transformation matrix:
    // | a c e |
    // | b d f |
    // | 0 0 1 |
    std::optional<std::array<double, 6>> transform;

    // When the color is exactly COLOR_NONE, it should
    // be treated as fill="none" or stroke="none", etc.
    // (If this color is specified explicitly, it will
    // be silently changed to a nearby nonzero color.)
    std::optional<uint32_t> fill_color;
    // Because this is specified separately in SVG, we
    // need to preserve it for correctly composing
    // sparse styles.
    std::optional<double> fill_opacity;

    std::optional<uint32_t> stroke_color;
    std::optional<double> stroke_opacity;
    std::optional<double> stroke_width;

    std::optional<LineCap> line_cap;
    std::optional<LineJoin> line_join;
    std::optional<double> miter_limit;

    std::optional<bool> use_even_odd_rule;

    std::optional<double> opacity;

    // An id in the symbol table. The transform from
    // this style object is also applied.
    std::optional<std::string> clip_path;
  };

  struct Node;

  struct G {
    Style style;
    std::vector<Node> children;
  };

  struct Path {
    std::vector<PathCommand> data;
  };

  // Since this is recursive we can't just make Node =
  // variant<G, Path>, so we have a simple wrapper.
  struct Node {
    std::variant<G, Path> v;
  };

  struct Doc {
    Node root;
    std::optional<std::array<double, 4>> view_box;

    // Elements with ids that are not rendered, because
    // they appear in <defs> or are <clipPath>. We could
    // extend this to other things like <mask> in the
    // future. The main use today is clip-path="";
    // we don't currently support <use href="">.
    // Dubious stuff like nested clipPaths is not supported.
    // Note that <clipPath> just becomes a G or Path here.
    std::unordered_map<std::string, SVG::G> defs;
  };

  // Optimizing version of the G{} constructor, which drops empty
  // children and collapses singleton groups without style.
  static SVG::Node MakeGroup(Style style, std::vector<Node> children);

  // Permissive parser.
  // This transforms various SVG elements (<polygon>, <circle>, etc.)
  // into equivalent paths. Style is applied only to <g> elements.
  //
  // If you want the original structure, use XML::Parse.
  static std::optional<Doc>
  Parse(std::string_view xml_bytes, std::string *error = nullptr);

  static Doc ParseOrDie(std::string_view xml_bytes);

  // Advanced stuff.

  // Parse (and interpret) path data. This eliminates relative
  // coordinates and special forms (for example a relative
  // 't' curve becomes the equivalent absolute 'Q' curve).
  //
  // The first command will be a MoveTo unless the entire path
  // is empty.
  static std::optional<std::vector<PathCommand>>
  InterpretPathData(std::string_view d, std::string *err);

  using Transform = std::array<double, 6>;

  static SVG::PathCommand TransformCommand(const Transform &tf,
                                           const SVG::PathCommand &cmd);

  // Concrete graphics state, which results from applying style.
  struct GraphicsState {
    Transform transform =
      {1.0, 0.0, 0.0, 1.0, 0.0, 0.0};

    uint32_t fill_color = 0x000000FF;
    double fill_opacity = 1.0;
    uint32_t stroke_color = COLOR_NONE;
    double stroke_opacity = 1.0;
    double stroke_width = 1.0;

    LineCap line_cap = LineCap::BUTT;
    LineJoin line_join = LineJoin::MITER;
    double miter_limit = 4.0;

    bool use_even_odd_rule = false;

    double opacity = 1.0;

    // ids from doc.defs, with the transform active at the time it is
    // used. Usually at most one of these.
    std::vector<std::pair<Transform, std::string>> clip_stack;
  };

  // True if nothing is set. If explicitly set to a default value,
  // this returns false.
  static bool IsDefault(const Style &style);

  static GraphicsState UpdateState(const GraphicsState &state,
                                   const Style &style);

  // Write the document back as SVG (XML text). The main use is
  // to verify that the parsed and normalized SVG has the correct
  // appearance by doing a round trip.
  static std::string ToSVG(const Doc &doc);

  // Consume an SVG number from the beginning of the string,
  // returning NAN if unsuccessful. This handles the permissive
  // stuff used in SVG path data like "10.5.5". Exposed mainly
  // for testing.
  static double ParseLeadingNumber(std::string_view *d);

  // Parse the transform-list SVG attribute, composing the
  // transforms into a single matrix. Might not support every
  // function, but you could fix it. This is exposed mainly
  // for testing.
  static std::optional<std::array<double, 6>> ParseTransformList(
      std::string_view s);
};

#endif
