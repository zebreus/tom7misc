
#include "svg.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <format>
#include <map>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "base/logging.h"
#include "base/print.h"
#include "base/stringprintf.h"
#include "util.h"
#include "xml.h"

[[maybe_unused]]
static inline constexpr std::tuple<uint8_t, uint8_t, uint8_t, uint8_t>
Unpack32(uint32_t color) {
  return {(uint8_t)((color >> 24) & 255),
          (uint8_t)((color >> 16) & 255),
          (uint8_t)((color >> 8) & 255),
          (uint8_t)(color & 255)};
}

static inline constexpr uint32_t
Pack32(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  return
    ((uint32_t)r << 24) |
    ((uint32_t)g << 16) |
    ((uint32_t)b << 8) |
    (uint32_t)a;
}


static void RemoveLeadingWhitespace(std::string_view *d) {
  while (!d->empty() && Util::IsWhitespace((*d)[0]))
    d->remove_prefix(1);
}

bool SVG::IsDefault(const Style &style) {
  return !(style.transform.has_value() ||
           style.fill_color.has_value() ||
           style.stroke_color.has_value() ||
           style.stroke_width.has_value() ||
           style.line_cap.has_value() ||
           style.line_join.has_value() ||
           style.miter_limit.has_value() ||
           style.use_even_odd_rule.has_value() ||
           style.opacity.has_value());
}

double SVG::ParseLeadingNumber(std::string_view *d) {
  RemoveLeadingWhitespace(d);
  if (d->empty()) return NAN;

  // PERF: This could be done without copying,
  // but it'll basically always fit in SSO.
  std::string s;

  // Consume at most one sign.
  auto ConsumeSign = [&d, &s]() {
      if (!d->empty()) {
        char c = (*d)[0];
        if (c == '-' || c == '+') {
          s.push_back(c);
          d->remove_prefix(1);
        }
      }
    };

  ConsumeSign();

  bool saw_period = false;
  bool saw_exponent = false;
  while (!d->empty()) {
    char c = (*d)[0];
    if (c >= '0' && c <= '9') {
      s.push_back(c);
      d->remove_prefix(1);

    } else if (c == '.') {
      if (saw_period || saw_exponent) {
        break;
      }
      saw_period = true;
      s.push_back(c);
      d->remove_prefix(1);

    } else if (c == 'e' || c == 'E') {

      if (saw_exponent) {
        break;
      }
      saw_exponent = true;
      s.push_back(c);
      d->remove_prefix(1);

      // Sign is allowed before exponent as well.
      ConsumeSign();

    } else {
      break;
    }
  }

  if (s.empty()) return NAN;
  return Util::ParseDouble(s, NAN);
}

static std::optional<double> ParseLength(std::string_view s) {
  Util::RemoveOuterWhitespace(&s);
  double d = SVG::ParseLeadingNumber(&s);
  if (!std::isfinite(d)) return std::nullopt;

  if (s.empty() || s == "px") {
    return {d};
  } else {
    Print("Unimplemented or invalid length unit: {}", s);
    return std::nullopt;
  }
}

// A percentage is multiplied by the unit length. Use 1.0 to convert
// 25% into 0.25, for example.
static std::optional<double> ParseNumberOrPercentage(std::string_view s,
                                                     double unit_length) {
  Util::RemoveOuterWhitespace(&s);
  double d = SVG::ParseLeadingNumber(&s);
  if (!std::isfinite(d)) return std::nullopt;

  if (s == "%") {
    return {(d / 100.0) * unit_length};
  } else if (s.empty()) {
    return {d};
  } else {
    Print("Expected number or percentage: {}", s);
    return std::nullopt;
  }
}

// Consume a pack of N numbers as a prefix of the input.
// Skips leading whitespace or commas. Handles
// tricky stuff like ".50.60" or "1.5e-100".
// Returns NaN if no number can be parsed.
template<size_t N>
std::optional<std::array<double, N>> Numbers(std::string_view *d) {
  auto ConsumeBetween = [&d]() {
      while (!d->empty() && (Util::IsWhitespace((*d)[0]) || (*d)[0] == ',')) {
        d->remove_prefix(1);
      }
    };

  ConsumeBetween();
  if (d->empty()) return std::nullopt;

  // Only if it begins a number.
  const char c = (*d)[0];
  std::array<double, N> a;
  if (c == '-' || c == '+' || c == '.' || (c >= '0' && c <= '9')) {
    // ... but then we are committed to reading N of them.
    for (size_t i = 0; i < N; i++) {
      a[i] = SVG::ParseLeadingNumber(d);
      ConsumeBetween();
    }
    return std::make_optional(a);
  } else {
    return std::nullopt;
  }
}

template<size_t N>
static bool NumbersOK(const std::array<double, N> &a) {
  for (double d : a) {
    if (!std::isfinite(d)) return false;
  }
  return true;
}

// Parse a color like "#fff" or "#123456".
static std::optional<uint32_t> ParseColor(std::string_view s_in) {
  // Nonzero, but fully transparent color.
  static constexpr uint32_t TRANSPARENT = 0x01010100;
  std::string lows = Util::lcase(s_in);
  std::string_view s(lows);

  Util::RemoveOuterWhitespace(&s);
  if (s.empty()) return {};

  // TODO: Could support the whole silly color name map here.
  if (s == "none") {
    return {SVG::COLOR_NONE};
  } else if (s == "transparent") {
    return {TRANSPARENT};
  } else if (s == "black") {
    return {0x000000FF};
  } else if (s == "white") {
    return {0xFFFFFFFF};
  } else if (s == "red") {
    return {0xFF0000FF};
  } else if (s == "blue") {
    return {0x0000FFFF};
  } else if (s == "green") {
    return {0x00FF00FF};
  }

  // In SVG, rgb() and rgba() are (surprisingly) equivalent.
  // We allow commas (legacy) and the modern '/'
  // separator for alpha, including mixing these
  // (nonstandard).
  if (Util::TryStripPrefix("rgba(", &s) ||
      Util::TryStripPrefix("rgb(", &s)) {
    auto Sep = Util::CharSpec(", /");
    auto Num = Util::CharSpec("-+0-9%");

    auto GetChannel = [&](bool color) {
        (void)Util::ConsumePrefixMatching(Sep, &s);
        std::string_view num = Util::ConsumePrefixMatching(Num, &s);
        return ParseNumberOrPercentage(num, color ? 255.0 : 1.0);
      };

    auto r = GetChannel(true);
    auto g = GetChannel(true);
    auto b = GetChannel(true);
    auto a = GetChannel(false);

    // Now expect a closing paren, or something is wrong?
    RemoveLeadingWhitespace(&s);
    if (s != ")")
      return {};

    if (!r.has_value() ||
        !g.has_value() ||
        !b.has_value())
      return {};


    uint8_t rr = std::clamp((int)r.value(), 0, 255);
    uint8_t gg = std::clamp((int)g.value(), 0, 255);
    uint8_t bb = std::clamp((int)b.value(), 0, 255);

    // Alpha is optional, though.
    if (a.has_value()) {
      uint8_t aa = std::clamp((int)(a.value() * 255.0), 0, 255);
      uint32_t c = Pack32(rr, gg, bb, aa);
      return (c == SVG::COLOR_NONE) ? TRANSPARENT : c;
    } else {
      return Pack32(rr, gg, bb, 0xFF);
    }

    return {};
  }

  if (s[0] == '#') {
    s.remove_prefix(1);
    for (char c : s)
      if (!Util::IsHexDigit(c))
        return {};

    if (s.size() == 3) {
      // #RGB
      uint8_t r = Util::HexDigitValue(s[0]);
      uint8_t g = Util::HexDigitValue(s[1]);
      uint8_t b = Util::HexDigitValue(s[2]);

      r |= (r << 4);
      g |= (g << 4);
      b |= (b << 4);
      return {Pack32(r, g, b, 0xFF)};

    } else if (s.size() == 4) {
      // #RGBA
      uint8_t r = Util::HexDigitValue(s[0]);
      uint8_t g = Util::HexDigitValue(s[1]);
      uint8_t b = Util::HexDigitValue(s[2]);
      uint8_t a = Util::HexDigitValue(s[3]);

      r |= (r << 4);
      g |= (g << 4);
      b |= (b << 4);
      a |= (a << 4);
      uint32_t c = Pack32(r, g, b, a);
      return {c == 0x00000000 ? TRANSPARENT : c};

    } else if (s.size() == 6) {
      // #RRGGBB
      uint8_t r = (Util::HexDigitValue(s[0]) << 4) | Util::HexDigitValue(s[1]);
      uint8_t g = (Util::HexDigitValue(s[2]) << 4) | Util::HexDigitValue(s[3]);
      uint8_t b = (Util::HexDigitValue(s[4]) << 4) | Util::HexDigitValue(s[5]);
      return {Pack32(r, g, b, 0xFF)};

    } else if (s.size() == 8) {
      // #RRGGBBAA
      uint8_t r = (Util::HexDigitValue(s[0]) << 4) | Util::HexDigitValue(s[1]);
      uint8_t g = (Util::HexDigitValue(s[2]) << 4) | Util::HexDigitValue(s[3]);
      uint8_t b = (Util::HexDigitValue(s[4]) << 4) | Util::HexDigitValue(s[5]);
      uint8_t a = (Util::HexDigitValue(s[6]) << 4) | Util::HexDigitValue(s[7]);
      uint32_t c = Pack32(r, g, b, a);
      return {c == 0x00000000 ? TRANSPARENT : c};

    } else {
      // Fall through...
    }
  }

  Print(stderr, "Unimplemented/invalid color attr: {}", s);
  return {};
}

using Transform = std::array<double, 6>;
static constexpr Transform IDENTITY_TRANSFORM =
  SVG::GraphicsState{}.transform;

// Gets M such that M*pt is Second * (First * pt), i.e. M = Second * First.
static Transform ComposeTransforms(
    const Transform &second,
    const Transform &first) {
  // Transformation matrix:
  // | a c e |   | g i k |
  // | b d f | * | h j l |
  // | 0 0 1 |   | 0 0 1 |
  const auto &[a, b, c, d, e, f] = second;
  const auto &[g, h, i, j, k, l] = first;
  return Transform{
    a * g + c * h,
    b * g + d * h,
    a * i + c * j,
    b * i + d * j,
    a * k + c * l + e,
    b * k + d * l + f,
  };
}

// Returns nullopt if there is no valid transform leading the string.
static std::optional<Transform> ParseLeadingTransform(
    std::string_view *s) {
  Util::RemoveOuterWhitespace(s);
  if (s->empty()) return {};

  if (Util::TryStripPrefix("matrix(", s)) {
    const auto &a = Numbers<6>(s);
    if (!NumbersOK(a.value())) {
      return std::nullopt;
    }

    RemoveLeadingWhitespace(s);
    if (!Util::TryStripPrefix(")", s))
      return std::nullopt;

    return std::make_optional(std::move(a.value()));
  }

  // TODO: Support other common transforms here.

  return std::nullopt;
}

std::optional<std::array<double, 6>> SVG::ParseTransformList(
    std::string_view s) {
  Transform transform = IDENTITY_TRANSFORM;

  for (;;) {
    Util::RemoveOuterWhitespace(&s);
    if (s.empty()) {
      // Success consuming the entire string.
      return std::make_optional(transform);
    }

    if (auto to = ParseLeadingTransform(&s)) {
      transform = ComposeTransforms(transform, to.value());
    } else {
      return std::nullopt;
    }
  }
}

SVG::Node SVG::MakeGroup(Style style, std::vector<Node> children) {
  std::vector<Node> progeny;
  progeny.reserve(children.size());
  for (Node &node : children) {
    if (SVG::G *g = std::get_if<SVG::G>(&node.v)) {
      // An empty node can always be removed, even if it has style.
      if (g->children.empty()) continue;

      // TODO: Remove nesting if g is unstyled.

      progeny.emplace_back(std::move(node));

    } else {
      progeny.emplace_back(std::move(node));
    }
  }
  children.clear();

  // No need for unstyled singleton nodes.
  if (progeny.size() == 1 && IsDefault(style)) {
    return std::move(progeny[0]);
  }

  // TODO: We can also collapse if the child is a group with no style,
  // but this may be subsumed by the flattening TODO above?

  return SVG::Node{SVG::G{
      .style = std::move(style),
      .children = std::move(progeny),
    }};
}

// A Cubic Beziér approximating a 90-degree elliptical arc.
// The start position for the arc is implied by the quadrant.
// For e.g. the bottom right (x and y both positive), the starting
// point is (cx + rx, cy).
//
// If you draw all four quadrants, you get (approximately) the ellipse
// centered at cx,cy with radii rx and ry.
static SVG::CubicBezier ApproxArc90(
    double cx, double cy,
    double rx, double ry,
    bool pos_x_quadrant,
    bool pos_y_quadrant) {

  // Standard way of approximating the curve. It is not exactly
  // an arc (this is impossible with cubic Beziér). This is the
  // location of the control point.
  static constexpr double KAPPA = 4.0 / 3.0 * (std::numbers::sqrt2 - 1.0);

  double kx = KAPPA * rx;
  double ky = KAPPA * ry;

  if (pos_x_quadrant && pos_y_quadrant) {
    // Bottom-Right.
    // Current point is implied at (cx + rx, cy)
    return SVG::CubicBezier{
      .cx1 = cx + rx,
      .cy1 = cy + ky,
      .cx2 = cx + kx,
      .cy2 = cy + ry,
      .x = cx,
      .y = cy + ry,
    };

  } else if (!pos_x_quadrant && pos_y_quadrant) {
    // Bottom-Left.
    // Current point implied at (cx, cy + ry)
    return SVG::CubicBezier{
      .cx1 = cx - kx,
      .cy1 = cy + ry,
      .cx2 = cx - rx,
      .cy2 = cy + ky,
      .x = cx - rx,
      .y = cy,
    };

  } else if (!pos_x_quadrant && !pos_y_quadrant) {
    // Top-Left.
    // Current point implied at (cx - rx, cy)
    return SVG::CubicBezier{
      .cx1 = cx - rx,
      .cy1 = cy - ky,
      .cx2 = cx - kx,
      .cy2 = cy - ry,
      .x = cx,
      .y = cy - ry,
    };

  } else {
    CHECK(pos_x_quadrant && !pos_y_quadrant);
    // Top-Right.
    // Current point implied at (cx, cy - ry)
    return SVG::CubicBezier{
      .cx1 = cx + kx,
      .cy1 = cy - ry,
      .cx2 = cx + rx,
      .cy2 = cy - ky,
      .x = cx + rx,
      .y = cy,
    };
  }
}

struct Converter {
  SVG::Doc doc;
  std::string error;

  // This will be moved into the doc at the end. But we use it
  // to check whether ids are known. When converting the defs
  // themselves, this will be nullopt.
  std::optional<std::unordered_map<std::string, SVG::Node>> doc_defs;

  std::optional<SVG::Style> RemoveStyleAttributes(XML::Node *node) {
    if (node->attrs.empty()) return std::nullopt;

    SVG::Style style;

    auto GetStripAttribute = [node](std::string_view a) ->
      std::optional<std::string> {
        auto it = node->attrs.find(std::string(a));
        if (it != node->attrs.end()) {
          std::string value = std::move(it->second);
          node->attrs.erase(it);
          return {value};
        }

        return std::nullopt;
      };

    // If there is a style attribute, decompose it into
    // attr=value pairs. These correctly override anything set on
    // attributes in this element, so we can just modify the
    // current attributes in place.
    if (auto so = GetStripAttribute("style")) {
      // XXX: Technically ';' can appear inside some css values, like
      // a quoted string (although we don't support any of these). We
      // could do like "split respecting strings and parens."
      std::vector<std::string> parts = Util::Tokenize(so.value(), ';');

      for (std::string_view part : parts) {
        Util::RemoveOuterWhitespace(&part);
        if (part.empty()) {
          continue;
        }

        std::string_view attr = Util::NextToken(&part, ':');
        Util::RemoveOuterWhitespace(&attr);
        std::string_view value = part;
        Util::RemoveOuterWhitespace(&value);
        if (attr.empty() || value.empty()) {
          error = "Could not parse style=\"\" attribute.";
          return {};
        }

        // XXX Could check whether the attribute is expected here?
        node->attrs[std::string(attr)] = value;
      }
    }

    bool had_style = false;
    // Attributes are all case-sensitive.
    if (auto fo = GetStripAttribute("fill")) {
      had_style = true;
      if (auto co = ParseColor(fo.value())) {
        style.fill_color = co;
      } else {
        error = "Invalid color in fill";
        return {};
      }
    }

    if (auto so = GetStripAttribute("fill-opacity")) {
      had_style = true;
      if (auto co = ParseNumberOrPercentage(so.value(), 1.0)) {
        style.fill_opacity = co;
      } else {
        error = "Invalid number in fill-opacity";
        return {};
      }
    }

    if (auto so = GetStripAttribute("stroke")) {
      had_style = true;
      if (auto co = ParseColor(so.value())) {
        style.stroke_color = co;
      } else {
        error = "Invalid color in stroke";
        return {};
      }
    }

    if (auto so = GetStripAttribute("stroke-opacity")) {
      had_style = true;
      if (auto co = ParseNumberOrPercentage(so.value(), 1.0)) {
        style.stroke_opacity = co;
      } else {
        error = "Invalid number in stroke-opacity";
        return {};
      }
    }

    if (auto so = GetStripAttribute("stroke-width")) {
      had_style = true;
      if (auto co = ParseLength(so.value())) {
        style.stroke_width = co;
      } else {
        error = "Invalid length in stroke-width";
        return {};
      }
    }

    if (auto so = GetStripAttribute("opacity")) {
      had_style = true;
      if (auto co = ParseNumberOrPercentage(so.value(), 1.0)) {
        style.opacity = co;
      } else {
        error = "Invalid length in opacity";
        return {};
      }
    }

    if (auto fo = GetStripAttribute("fill-rule")) {
      if (fo.value() == "evenodd") {
        style.use_even_odd_rule = {true};
      } else if (fo.value() == "nonzero") {
        style.use_even_odd_rule = {false};
      } else {
        // e.g. "inherit" or incomprehensible.
      }
    }

    if (auto po = GetStripAttribute("clip-path")) {
      Print("CLIP PATH!\n");
      had_style = true;
      std::string_view s(po.value());
      Util::RemoveOuterWhitespace(&s);
      if (Util::TryStripPrefix("url(#", &s) &&
          Util::TryStripSuffix(")", &s)) {
        Print("   ...  yes URL {}\n", s);
        if (doc_defs.has_value()) {
          Print("  ... Yes have doc defs\n");
          if (doc_defs.value().contains(std::string(s))) {
            Print("  ... {}\n", s);
            style.clip_path = {std::string(s)};
          } else {
            Print(" .. but it was not found?");
            error = "Unknown reference in clip-path.";
            return {};
          }
        } else {
          error = "Not supported to use url references here. Clip path "
            "inside <defs>?";
          return {};
        }

      } else {
        error = "In clip-path, only url(#A) references are supported.";
        return {};
      }
    }

    // Only if we actually saw style attributes.
    return had_style ? std::make_optional(style) : std::nullopt;
  }

  // Modifies the XML tree in place.
  SVG::Node ConvertRec(XML::Node *node) {

    if (node->type == XML::NodeType::Text) {
      // Must be all whitespace for the subset we parse.
      std::string_view s(node->contents);
      RemoveLeadingWhitespace(&s);
      if (!s.empty()) {
        error = "Node with non-whitespace text content.";
        return {};
      }
      return {SVG::G{.style = {}, .children = {}}};
    } else {
      CHECK(node->type == XML::NodeType::Element);

      const std::string &tag = node->tag;

      std::optional<SVG::Style> maybe_style =
        RemoveStyleAttributes(node);
      if (!error.empty())
        return {};

      if (tag == "g") {
        SVG::G g;

        std::vector<SVG::Node> children;
        for (XML::Node &child : node->children) {
          children.emplace_back(ConvertRec(&child));
        }

        return SVG::MakeGroup(
            maybe_style.has_value() ? maybe_style.value() : SVG::Style{},
            std::move(children));

      } else if (maybe_style.has_value()) {
        // Otherwise, if there are style attributes, insert
        // a singleton group so that we have a place to
        // put them.
        //
        // We know that the recusion is well-founded because we must
        // have removed at least one attribute.
        return SVG::MakeGroup(maybe_style.value(), {ConvertRec(node)});

      } else if (tag == "path") {
        // This is the main thing we're interested in, and we
        // transform various other tags into this.
        auto dit = node->attrs.find("d");
        if (dit == node->attrs.end()) {
          // No path data. A no-op.
          return SVG::Node(SVG::G());
        }

        const std::string &d = dit->second;

        Print("Path data: {}\n", d);

        if (std::optional<std::vector<SVG::PathCommand>> opathdata =
            SVG::InterpretPathData(d, &error)) {

          if (opathdata.value().empty()) {
            Print("Empty path data.\n");
            return SVG::Node(SVG::G());
          }

          Print("Got {} path commands.\n", opathdata.value().size());
          return SVG::Node(SVG::Path{.data = std::move(opathdata.value())});

        } else {
          error = "Couldn't parse path data.";
          Print("{}\n", error);
          return {};
        }

      } else if (tag == "polygon") {
        auto pit = node->attrs.find("points");
        if (pit == node->attrs.end()) {
          return SVG::Node(SVG::G());
        }

        std::string d = std::format("M {} Z", pit->second);

        node->tag = "path";
        node->attrs.erase(pit);
        node->attrs["d"] = std::move(d);
        return ConvertRec(node);

      } else if (tag == "rect") {
        double x = Util::ParseDouble(node->attrs["x"], 0.0);
        double y = Util::ParseDouble(node->attrs["y"], 0.0);
        double w = Util::ParseDouble(node->attrs["width"], 0.0);
        double h = Util::ParseDouble(node->attrs["height"], 0.0);

        // Well-defined no-op if zero area.
        if (w == 0.0 || h == 0.0) {
          return SVG::Node(SVG::G());
        }

        double rx = 0.0;
        double ry = 0.0;
        bool has_rx = node->attrs.contains("rx");
        bool has_ry = node->attrs.contains("ry");

        if (has_rx) rx = Util::ParseDouble(node->attrs["rx"], 0.0);
        if (has_ry) ry = Util::ParseDouble(node->attrs["ry"], 0.0);

        if (has_rx && !has_ry) ry = rx;
        if (has_ry && !has_rx) rx = ry;

        // If negative, then this is invalid.
        if (w < 0.0 || h < 0.0 || rx < 0.0 || ry < 0.0) {
          error = "Invalid <rect>";
          return {};
        }

        // Clamp radii to half dimensions.
        bool has_horiz = true;
        if (rx >= w * 0.5) {
          rx = w * 0.5;
          has_horiz = false;
        }

        bool has_vert = true;
        if (ry >= h * 0.5) {
          ry = h * 0.5;
          has_vert = false;
        }

        SVG::Path path;

        // Top left (after the arc).
        path.data.emplace_back(SVG::MoveTo{ .x = x + rx, .y = y });

        // Only draw the horizontal edge if it has positive length.
        if (has_horiz) {
          path.data.emplace_back(SVG::LineTo{ .x = x + w - rx, .y = y });
        }

        // Only draw the top-right corner as an arc if it has a radius.
        if (rx > 0.0 || ry > 0.0) {
          path.data.emplace_back(
              ApproxArc90(x + w - rx, y + ry,
                          rx, ry,
                          true, false));
        }

        // Right edge.
        if (has_vert) {
          path.data.emplace_back(SVG::LineTo{ .x = x + w, .y = y + h - ry });
        }

        if (rx > 0.0 || ry > 0.0) {
          path.data.emplace_back(
              ApproxArc90(x + w - rx, y + h - ry,
                          rx, ry,
                          true, true));
        }

        // Bottom edge.
        if (has_horiz) {
          path.data.emplace_back(SVG::LineTo{ .x = x + rx, .y = y + h });
        }

        if (rx > 0.0 || ry > 0.0) {
          path.data.emplace_back(
              ApproxArc90(x + rx, y + h - ry,
                          rx, ry,
                          false, true));
        }

        // Left edge.
        if (has_vert) {
          path.data.emplace_back(SVG::LineTo{ .x = x, .y = y + ry });
        }

        if (rx > 0.0 || ry > 0.0) {
            path.data.emplace_back(
                ApproxArc90(x + rx, y + ry,
                            rx, ry,
                            false, false));
        }

        path.data.emplace_back(SVG::ClosePath());
        return {SVG::Node(std::move(path))};

      } else if (tag == "ellipse" || tag == "circle") {

        double cx = Util::ParseDouble(node->attrs["cx"], 0.0);
        double cy = Util::ParseDouble(node->attrs["cy"], 0.0);
        double rx = 0.0, ry = 0.0;
        if (tag == "circle") {
          double r = Util::ParseDouble(node->attrs["r"], 0.0);
          rx = ry = r;
        } else {
          rx = Util::ParseDouble(node->attrs["rx"], 0.0);
          ry = Util::ParseDouble(node->attrs["ry"], 0.0);
        }

        if (rx == 0.0 || ry == 0.0) {
          // Explicitly empty geometry (not even stroke).
          return SVG::Node(SVG::G());
        }

        if (rx <= 0.0 || ry <= 0.0 ||
            !NumbersOK(std::array{cx, cy, rx, ry})) {
          error = "Invalid circle or ellipse";
          return {};
        }

        SVG::Path path;
        path.data.emplace_back(SVG::MoveTo{
            .x = cx + rx,
            .y = cy,
          });
        path.data.emplace_back(ApproxArc90(cx, cy, rx, ry,
                                           true, true));
        path.data.emplace_back(ApproxArc90(cx, cy, rx, ry,
                                           false, true));
        path.data.emplace_back(ApproxArc90(cx, cy, rx, ry,
                                           false, false));
        path.data.emplace_back(ApproxArc90(cx, cy, rx, ry,
                                           true, false));

        // We land back at the start with the last arc, but
        // close the path for hygeine.
        path.data.emplace_back(SVG::ClosePath());

        return {SVG::Node(std::move(path))};

      } else if (tag == "defs") {
        // Handled in the first phase, and unrendered
        // by definition (hehe).
        return SVG::Node(SVG::G{});

      } else if (IsUnrendered(tag)) {
        // Skip unrendered nodes.
        return SVG::Node(SVG::G{});

      } else {

        LOG(FATAL) << "Unimplemented tag: " << tag;
      }

    }

    LOG(FATAL) << "Unimplemented";
    return {};
  }

  // tags that are not rendered and only useful as defs.
  // We skip these in the second phase, and put them in the defs
  // map in the first phase (if they have ids).
  static bool IsUnrendered(std::string_view tag) {
    return tag == "clipPath" ||
      tag == "symbol" ||
      tag == "marker" ||
      tag == "mask" ||
      tag == "linearGradient" ||
      tag == "radialGradient" ||
      tag == "pattern" ||
      tag == "filter";
  }

  // In addition to Unrendered nodes, anything in <defs> is unrendered
  // and only useful if it has an id. We collect these for the sake
  // of future support for <use>.
  //
  // Process the subtree of a <defs>. Finds all nodes with id="" and
  // translates them, placing them in the defs map. Returns false
  // and sets the error member on an invalid SVG.
  bool CollectDefs(XML::Node *node,
                   std::unordered_map<std::string, SVG::Node> *defs) {
    Print("CollectDefs:\n");
    if (node->type == XML::NodeType::Element) {

      if (auto iit = node->attrs.find("id"); iit != node->attrs.end()) {
        const std::string id = iit->second;
        Print("CollectDefs {}\n", id);
        if (id.empty() || defs->contains(id)) {
          error = "Node id invalid or not unique";
          return false;
        }

        (*defs)[id] = ConvertUnrendered(node);
        Print("Now there are {} defs\n", defs->size());
        return error.empty();

      } else {
        Print("CollectDefs {} no id\n", node->tag);
        for (XML::Node &child : node->children) {
          if (!CollectDefs(&child, defs)) {
            return false;
          }
        }
      }
    }

    return true;
  }

  SVG::Node ConvertUnrendered(XML::Node *node) {
    // In the future, this could use the tag type (e.g. clipPath)
    // to do more checking, etc.. But we just treat all of these
    // as transparent groups today.
    node->tag = "g";
    return ConvertRec(node);
  }

  // Recursively find unrendered nodes that have ids, like <clipPath>
  // and anything within <defs>. Puts them in defs and deletes their
  // contents. Returns false and sets the error member if the SVG is
  // invalid.
  bool FindUnrenderedNodes(XML::Node *node,
                           std::unordered_map<std::string, SVG::Node> *defs) {
    if (node->type == XML::NodeType::Element) {
      if (node->tag == "defs") {
        for (XML::Node &child : node->children) {
          if (!CollectDefs(&child, defs)) {
            return false;
          }
        }
        // Clean it up.
        node->children.clear();

      } else if (node->attrs.contains("id") &&
                 IsUnrendered(node->tag)) {

        const std::string id = node->attrs["id"];
        Print("Found {} with id={}\n", node->tag, id);
        if (id.empty() || defs->contains(id)) {
          error = "Node id invalid or not unique";
          return false;
        }

        (*defs)[id] = ConvertUnrendered(node);
        // Clean it up; the second phase should ignore it
        // because it is an Unrendered tag.
        node->children.clear();
        node->attrs.clear();
        return error.empty();

      } else {
        for (XML::Node &child : node->children) {
          if (!FindUnrenderedNodes(&child, defs)) {
            return false;
          }
        }
      }
    }

    return true;
  }

  // Consumes root, leaving it in an unspecified state.
  void Convert(XML::Node *root) {
    if (root->tag == "svg") {
      auto vit = root->attrs.find("viewBox");
      if (vit != root->attrs.end()) {
        std::string_view s = vit->second;
        if (const auto &a = Numbers<4>(&s)) {
          doc.view_box = a;
        } else {
          error = "Bad viewBox";
          return;
        }
      }

      // Ignore other attributes.
      root->attrs.clear();

      // We need to find nodes we might refer to by id.
      // We don't just do this for anything with an id, though,
      // because it is not unusual to find an SVG where every
      // node has a unique generated id. We collect the nodes
      // that would not be rendered in the next pass.
      std::unordered_map<std::string, SVG::Node> defs;
      if (!FindUnrenderedNodes(root, &defs)) {
        if (error.empty()) error = "Error parsing defs?";
        return;
      }

      // Now the defs are available for use.
      doc_defs = std::make_optional(std::move(defs));
      for (const auto &[id, node] : doc_defs.value()) {
        Print("DEF {} = ...\n", id);
      }
      root->tag = "g";
      doc.root = ConvertRec(root);

      CHECK(doc_defs.has_value());
      doc.defs = std::move(doc_defs.value());

    } else {
      error = "Root of SVG must be an <svg> tag.";
    }
  }

};


std::optional<SVG::Doc>
SVG::Parse(std::string_view xml_bytes, std::string *error) {
  std::optional<XML::Node> oroot = XML::Parse(xml_bytes, error);
  if (!oroot.has_value()) return std::nullopt;

  Converter converter;
  converter.Convert(&oroot.value());
  if (!converter.error.empty()) {
    if (error != nullptr) *error = std::move(converter.error);
    return std::nullopt;
  } else {
    return {converter.doc};
  }
}

SVG::Doc SVG::ParseOrDie(std::string_view xml_bytes) {
  std::string error;
  auto eo = Parse(xml_bytes, &error);
  CHECK(eo.has_value()) << "Unable to parse SVG: " << error;
  return std::move(eo.value());
}

std::optional<std::vector<SVG::PathCommand>>
SVG::InterpretPathData(std::string_view d, std::string *error) {
  std::vector<SVG::PathCommand> cmds;
  Util::RemoveOuterWhitespace(&d);
  // Completely empty is valid.
  if (d.empty()) return {cmds};

  auto WriteErr = [&error](const char *err) {
      if (error != nullptr) {
        *error = err;
      }
    };

  // Last pen position for relative commands.
  double px = 0.0, py = 0.0;
  // Also last control point for curve shorthands.
  // (Or the last point when not a curve command.)
  [[maybe_unused]] double last_cx = 0.0, last_cy = 0.0;
  // Each time we MoveTo, we save the start of the
  // path for the sake of closing it.
  double startx = 0.0, starty = 0.0;
  for (;;) {
    RemoveLeadingWhitespace(&d);

    if (d.empty()) {
      break;
    }

    char cmd = d[0];
    d.remove_prefix(1);

    switch (cmd) {
    case 'M':
    case 'm': {
      bool first = true;
      while (const auto &a = Numbers<2>(&d)) {
        if (!NumbersOK(a.value())) {
          WriteErr("Expected 2 numbers after MoveTo.");
          return std::nullopt;
        }

        double nx, ny;
        std::tie(nx, ny) = a.value();
        if (cmd == 'm') {
          nx += px;
          ny += py;
        }

        // MoveTo has a special case for repeated arguments,
        // since moving multiple times would have no point.
        if (first) {
          cmds.emplace_back(MoveTo{.x = nx, .y = ny});
          startx = nx;
          starty = ny;
          first = false;
        } else {
          cmds.emplace_back(LineTo{.x = nx, .y = ny});
        }

        last_cx = px = nx;
        last_cy = py = ny;
      }
      break;
    }

    case 'L':
    case 'l': {
      while (const auto &a = Numbers<2>(&d)) {
        if (!NumbersOK(a.value())) {
          WriteErr("Expected 2 numbers after LineTo.");
          return std::nullopt;
        }

        double nx, ny;
        std::tie(nx, ny) = a.value();
        if (cmd == 'l') {
          nx += px;
          ny += py;
        }

        cmds.emplace_back(LineTo{.x = nx, .y = ny});
        last_cx = px = nx;
        last_cy = py = ny;
      }
      break;
    }

    case 'H':
    case 'h': {
      while (const auto &a = Numbers<1>(&d)) {
        if (!NumbersOK(a.value())) {
          WriteErr("Expected 1 number after Horizontal LineTo.");
          return std::nullopt;
        }

        double n = a.value()[0];
        if (cmd == 'h') {
          n += px;
        }

        cmds.emplace_back(LineTo{.x = n, .y = py});

        last_cx = px = n;
        last_cy = py;
      }
      break;
    }

    case 'V':
    case 'v': {
      while (const auto &a = Numbers<1>(&d)) {
        if (!NumbersOK(a.value())) {
          WriteErr("Expected 1 number after Vertical LineTo.");
          return std::nullopt;
        }

        double n = a.value()[0];
        if (cmd == 'v') {
          n += py;
        }

        cmds.emplace_back(LineTo{.x = px, .y = n});

        last_cx = px;
        last_cy = py = n;
      }
      break;
    }

    case 'C':
    case 'c': {
      while (const auto &a = Numbers<6>(&d)) {
        if (!NumbersOK(a.value())) {
          WriteErr("Expected 6 numbers after CubicBezier.");
          return std::nullopt;
        }
        double x1, y1, x2, y2, x, y;
        std::tie(x1, y1, x2, y2, x, y) = a.value();

        if (cmd == 'c') {
          x1 += px;
          y1 += py;
          x2 += px;
          y2 += py;
          x += px;
          y += py;
        }

        cmds.emplace_back(CubicBezier{
            .cx1 = x1,
            .cy1 = y1,
            .cx2 = x2,
            .cy2 = y2,
            .x = x,
            .y = y,
        });

        px = x;
        py = y;
        last_cx = x2;
        last_cy = y2;
      }
      break;
    }

    case 'S':
    case 's': {
      while (const auto &a = Numbers<4>(&d)) {
        if (!NumbersOK(a.value())) {
          WriteErr("Expected 4 numbers after Smooth CubicBezier.");
          return std::nullopt;
        }
        double x2, y2, x, y;
        std::tie(x2, y2, x, y) = a.value();

        if (cmd == 's') {
          x2 += px;
          y2 += py;
          x += px;
          y += py;
        }

        // First control point is implicit (reflected version of previous).
        double x1 = (2.0 * px) - last_cx;
        double y1 = (2.0 * py) - last_cy;

        cmds.emplace_back(CubicBezier{
            .cx1 = x1,
            .cy1 = y1,
            .cx2 = x2,
            .cy2 = y2,
            .x = x,
            .y = y,
        });

        px = x;
        py = y;
        last_cx = x2;
        last_cy = y2;
      }
      break;
    }

    case 'Q':
    case 'q': {
      while (const auto &a = Numbers<4>(&d)) {
        if (!NumbersOK(a.value())) {
          WriteErr("Expected 4 numbers after Quadratic Bezier.");
          return std::nullopt;
        }
        double x1, y1, x, y;
        std::tie(x1, y1, x, y) = a.value();

        if (cmd == 'q') {
          x1 += px;
          y1 += py;
          x += px;
          y += py;
        }

        cmds.emplace_back(QuadBezier{
            .cx = x1,
            .cy = y1,
            .x = x,
            .y = y,
        });

        px = x;
        py = y;

        last_cx = x1;
        last_cy = y1;
      }
      break;
    }

    case 'T':
    case 't': {
      while (const auto &a = Numbers<2>(&d)) {
        if (!NumbersOK(a.value())) {
          WriteErr("Expected 2 numbers after Smooth Quadratic.");
          return std::nullopt;
        }
        double x, y;
        std::tie(x, y) = a.value();

        if (cmd == 't') {
          x += px;
          y += py;
        }

        // Calculate reflection of previous control point.
        // e.g. we have v = (c - p)
        // and the reflected point is then p - v,
        // So p - (c - p) = 2p - c.
        double cx = (2.0 * px) - last_cx;
        double cy = (2.0 * py) - last_cy;

        cmds.emplace_back(QuadBezier{
            .cx = cx,
            .cy = cy,
            .x = x,
            .y = y,
        });

        px = x;
        py = y;
        last_cx = cx;
        last_cy = cy;
      }
      break;
    }

    case 'Z':
    case 'z':
      cmds.emplace_back(ClosePath{});
      // Move current position in case there are more commands.
      last_cx = px = startx;
      last_cy = py = starty;
      break;

      // TODO: More commands here.

    default:
      Print("Unimplemented path command {:c}\n", cmd);
      WriteErr("unimplemented command");
      return std::nullopt;
      break;
    }

  }

  CHECK(!cmds.empty());
  if (!std::holds_alternative<MoveTo>(cmds[0])) {
    WriteErr("First command not MoveTo.");
    return std::nullopt;
  }

  return {cmds};
}

static std::string Rtos(double d) {
  return std::format("{:.4f}", d);
}

static std::string PathDataString(const std::vector<SVG::PathCommand> &cmds) {
  std::string out;
  for (const SVG::PathCommand &cmd : cmds) {
    if (const SVG::MoveTo *m = std::get_if<SVG::MoveTo>(&cmd)) {
      AppendFormat(&out, "M {} {}", Rtos(m->x), Rtos(m->y));
    } else if (const SVG::LineTo *l = std::get_if<SVG::LineTo>(&cmd)) {
      AppendFormat(&out, "L {} {}", Rtos(l->x), Rtos(l->y));
    } else if (const SVG::CubicBezier *c =
                 std::get_if<SVG::CubicBezier>(&cmd)) {
      AppendFormat(&out, "C {} {} {} {} {} {}",
                   Rtos(c->cx1), Rtos(c->cy1),
                   Rtos(c->cx2), Rtos(c->cy2),
                   Rtos(c->x), Rtos(c->y));
    } else if (const SVG::QuadBezier *q =
                 std::get_if<SVG::QuadBezier>(&cmd)) {
      AppendFormat(&out, "Q {} {} {} {}",
                   Rtos(q->cx), Rtos(q->cy),
                   Rtos(q->x), Rtos(q->y));
    } else if ([[maybe_unused]] const SVG::ClosePath *z =
                 std::get_if<SVG::ClosePath>(&cmd)) {
      out.append("Z");
    }
  }

  return out;
}

struct Unconverter {
  explicit Unconverter(const SVG::Doc &doc) : doc(doc) {}

  const SVG::Doc &doc;
  // Assumes that the definitions are used in a consistent way,
  // e.g. a given id always as clip-path.
  std::map<std::string, std::string> used_defs;

  void AppendSVG(int depth, const SVG::Node &node, std::string *out) {
    if (const SVG::G *g = std::get_if<SVG::G>(&node.v)) {
      const SVG::Style &style = g->style;
      AppendFormat(out, "{}<g", std::string(depth, ' '));

      if (style.transform.has_value()) {
        const auto &[a, b, c, d, e, f] = style.transform.value();
        AppendFormat(out, " transform=\"matrix({:.11g} {:.11g} {:.11g} "
                     "{:.11g} {:.11g} {:.11g})\"", a, b, c, d, e, f);
      }

      // Note: Illustrator does not seem to support rgba colors for fill
      // or stroke! rgb(255 255 0 / 0.5) might be a good compromise since
      // illustrator will treat that as opaque yellow, but compliant
      // parsers will treat it as 50% transparent yellow.
      //
      // try svgviewer.dev instead.
      if (style.fill_color.has_value()) {
        if (style.fill_color.value() == SVG::COLOR_NONE) {
          out->append(" fill=\"none\"");
        } else {
          AppendFormat(out, " fill=\"#{:08X}\"", style.fill_color.value());
        }
      }

      if (style.fill_opacity.has_value()) {
        AppendFormat(out, " fill-opacity=\"{}\"",
                     Rtos(style.fill_opacity.value()));
      }

      if (style.stroke_color.has_value()) {
        if (style.stroke_color.value() == SVG::COLOR_NONE) {
          out->append(" stroke=\"none\"");
        } else {
          AppendFormat(out, " stroke=\"#{:08X}\"", style.stroke_color.value());
        }
      }

      if (style.stroke_opacity.has_value()) {
        AppendFormat(out, " stroke-opacity=\"{}\"",
                     Rtos(style.stroke_opacity.value()));
      }

      if (style.stroke_opacity.has_value()) {
        AppendFormat(out, " stroke-width=\"{}px\"",
                     Rtos(style.stroke_width.value()));
      }

      if (style.line_cap.has_value()) {
        LOG(FATAL) << "Unimplemented line_cap";
      }
      if (style.line_join.has_value()) {
        LOG(FATAL) << "Unimplemented line_join";
      }
      if (style.miter_limit.has_value()) {
        LOG(FATAL) << "Unimplemented miter_limit";
      }

      if (style.use_even_odd_rule.has_value()) {
        out->append(style.use_even_odd_rule.value() ?
                    " fill-rule=\"evenodd\"" :
                    " fill-rule=\"nonzero\"");
      }

      if (style.opacity.has_value()) {
        AppendFormat(out, " opacity=\"{}\"",
                     Rtos(style.opacity.value()));
      }

      if (style.clip_path.has_value()) {
        UseAs("clipPath", style.clip_path.value());
        AppendFormat(out, " clip-path=\"url(#{})\"",
                     style.clip_path.value());
      }

      out->append(">\n");

      for (const auto &c : g->children) {
        AppendSVG(depth + 2, c, out);
      }
      AppendFormat(out, "{}</g>\n", std::string(depth, ' '));
    } else if (const SVG::Path *path = std::get_if<SVG::Path>(&node.v)) {
      std::string d = PathDataString(path->data);
      AppendFormat(out, "{}<path d=\"{}\" />\n", std::string(depth, ' '), d);
    } else {
      LOG(FATAL) << "Bad variant?";
   }
  }

  void UseAs(std::string_view tag_type, std::string_view idv) {
    Print("--------- Use {} {}\n", tag_type, idv);
    std::string id(idv);
    auto it = doc.defs.find(id);
    CHECK(it != doc.defs.end()) << "Unresolved id: " << id;
    // Could check consistent use here.
    if (used_defs.contains(id)) return;
    std::string rendered = std::format("  <{} id=\"{}\">\n", tag_type, id);
    AppendSVG(4, it->second, &rendered);
    AppendFormat(&rendered, "  </{}>\n", tag_type);
    used_defs[id] = std::move(rendered);
  }

  void AppendDefs(std::string *out) {
    if (used_defs.empty()) return;

    out->append("  <defs>\n");
    for (const auto &[id_, elt] : used_defs) {
      out->append(elt);
    }
    out->append("  </defs>\n");
  }
};


std::string SVG::ToSVG(const SVG::Doc &doc) {
  std::string out = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<svg xmlns=\"http://www.w3.org/2000/svg\" version=\"1.1\"";

  Unconverter unc(doc);
  if (doc.view_box.has_value()) {
    const auto &[x, y, w, h] = doc.view_box.value();
    AppendFormat(&out, " viewBox=\"{} {} {} {}\"",
                 Rtos(x), Rtos(y), Rtos(w), Rtos(h));
  }
  out.append(">\n");

  std::string body;
  unc.AppendSVG(2, doc.root, &body);

  unc.AppendDefs(&out);
  out.append(body);

  out.append("</svg>\n");
  return out;
}
