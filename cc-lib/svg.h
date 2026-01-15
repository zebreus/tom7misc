
// In progress (not working!): Parser for a simple subset of SVG.

#ifndef _CC_LIB_SVG_H
#define _CC_LIB_SVG_H

#include <array>
#include <optional>
#include <string>
#include <string_view>
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

  struct CubicBezier {
    double cx1, cy1;
    double cx2, cy2;
    double x, y;
  };

  struct QuadBezier {
    // Control point.
    double cx, cy;
    // Destination.
    double x, y;
  };

  struct ClosePath {
  };

  using PathCommand = std::variant<MoveTo, LineTo, CubicBezier, QuadBezier,
                                   ClosePath>;

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

  struct Style {
    std::optional<std::array<double, 6>> transform;

    std::optional<uint32_t> fill_color;
    std::optional<uint32_t> stroke_color;
    std::optional<double> stroke_width;

    std::optional<LineCap> line_cap;
    std::optional<LineJoin> line_join;
    std::optional<double> miter_limit;

    std::optional<bool> use_even_odd_rule;

    std::optional<double> opacity;
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
    // TODO: Symbol table (for e.g. clipPath).
  };

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

  // Concrete graphics state, which results from applying style.
  struct GraphicsState {
    std::array<double, 6> transform =
      {1.0, 0.0, 0.0, 1.0, 0.0, 0.0};

    uint32_t fill_color = 0xFF0000FF;
    uint32_t stroke_color = 0x00000000;
    double stroke_width = 1.0;

    LineCap line_cap = LineCap::BUTT;
    LineJoin line_join = LineJoin::MITER;
    double miter_limit = 4.0;

    bool use_even_odd_rule = false;

    double opacity = 1.0;
  };

  static GraphicsState UpdateState(const GraphicsState &state,
                                   const Style &style);

  // Consume an SVG number from the beginning of the string,
  // returning NAN if unsuccessful. This handles the permissive
  // stuff used in SVG path data like "10.5.5". Exposed mainly
  // for testing.
  static double ParseLeadingNumber(std::string_view *d);
};

#endif
