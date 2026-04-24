
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
#include <unordered_set>
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

inline static SVG::CubicBezier QuadraticBezier(
    double start_x, double start_y,
    double cx, double cy,
    double x, double y) {
  return SVG::CubicBezier{
    .cx1 = start_x + (2.0 / 3.0) * (cx - start_x),
    .cy1 = start_y + (2.0 / 3.0) * (cy - start_y),
    .cx2 = x + (2.0 / 3.0) * (cx - x),
    .cy2 = y + (2.0 / 3.0) * (cy - y),
    .x = x,
    .y = y,
  };
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
           style.opacity.has_value() ||
           style.clip_path.has_value() ||
           style.font_family.has_value() ||
           style.font_size.has_value());
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
    Print(stderr, "Unimplemented or invalid length unit: {}", s);
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
    Print(stderr, "Expected number or percentage: {}", s);
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

// Parse the entire string s as an arbitrarily long sequence of numbers,
// or return nullopt if it cannot.
static std::optional<std::vector<double>> AllNumbers(std::string_view s) {
  std::vector<double> ret;
  for (;;) {
    RemoveLeadingWhitespace(&s);
    if (s.empty()) break;

    auto od = Numbers<1>(&s);
    if (!od.has_value() || !std::isfinite(od.value()[0]))
      return std::nullopt;

    ret.push_back(od.value()[0]);
  }

  return {ret};
}

// Returns nullopt if there is no valid transform leading the string.
static std::optional<Transform> ParseLeadingTransform(
    std::string_view *s) {

  auto DegToRad = [](double deg) { return deg * (std::numbers::pi / 180.0); };
  auto NotClose = [](char c) { return c != ')'; };

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
  } else if (Util::TryStripPrefix("translate(", s)) {

    std::string_view nums = Util::ConsumePrefixMatching(NotClose, s);
    if (!Util::TryStripPrefix(")", s))
      return std::nullopt;

    if (auto args = AllNumbers(nums)) {
      if (args.value().size() == 1) {
        return std::make_optional(
            Transform{1.0, 0.0, 0.0, 1.0, args.value()[0], 0.0});
      } else if (args.value().size() == 2) {
        return std::make_optional(
            Transform{1.0, 0.0, 0.0, 1.0, args.value()[0], args.value()[1]});
      }
    }

    // Failed.
    return std::nullopt;
  } else if (Util::TryStripPrefix("scale(", s)) {

    std::string_view nums = Util::ConsumePrefixMatching(NotClose, s);
    if (!Util::TryStripPrefix(")", s))
      return std::nullopt;

    if (auto args = AllNumbers(nums)) {
      if (args.value().size() == 1) {
        return std::make_optional(
            Transform{args.value()[0], 0.0, 0.0, args.value()[0], 0.0, 0.0});
      } else if (args.value().size() == 2) {
        return std::make_optional(
            Transform{args.value()[0], 0.0, 0.0, args.value()[1], 0.0, 0.0});
      }
    }

    return std::nullopt;
  } else if (Util::TryStripPrefix("rotate(", s)) {

    std::string_view nums = Util::ConsumePrefixMatching(NotClose, s);
    if (!Util::TryStripPrefix(")", s))
      return std::nullopt;

    auto args = AllNumbers(nums);
    if (!args.has_value())
      return std::nullopt;

    if (!(args.value().size() == 1 ||
          args.value().size() == 3))
      return std::nullopt;

    double a = DegToRad(args.value()[0]);
    double cosa = std::cos(a);
    double sina = std::sin(a);

    // Center of rotation (zero if one-arg).
    double cx = 0.0, cy = 0.0;
    if (args.value().size() == 3) {
      cx = args.value()[1];
      cy = args.value()[2];
    }

    double e = cx - (cx * cosa - cy * sina);
    double f = cy - (cx * sina + cy * cosa);
    return Transform{cosa, sina, -sina, cosa, e, f};

  } else if (Util::TryStripPrefix("skewX(", s)) {
    std::string_view nums = Util::ConsumePrefixMatching(NotClose, s);
    if (!Util::TryStripPrefix(")", s))
      return std::nullopt;

    auto args = AllNumbers(nums);
    if (!args.has_value() || args.value().size() != 1)
      return std::nullopt;

    double a = DegToRad(args.value()[0]);
    return Transform{1.0, 0.0, std::tan(a), 1.0, 0.0, 0.0};

  } else if (Util::TryStripPrefix("skewY(", s)) {
    std::string_view nums = Util::ConsumePrefixMatching(NotClose, s);
    if (!Util::TryStripPrefix(")", s))
      return std::nullopt;

    auto args = AllNumbers(nums);
    if (!args.has_value() || args.value().size() != 1)
      return std::nullopt;

    double a = DegToRad(args.value()[0]);
    return Transform{1.0, std::tan(a), 0.0, 1.0, 0.0, 0.0};
  }

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

SVG::Node SVG::MakeGroup(std::optional<Style> style,
                         std::vector<Node> children) {
  std::vector<Node> progeny;
  progeny.reserve(children.size());
  for (Node &node : children) {
    if (SVG::G *g = std::get_if<SVG::G>(&node.v)) {
      // An empty node can always be removed, even if it has style.
      if (g->children.empty()) continue;

      if (IsDefault(g->style)) {
        // Flatten children, since the group does nothing but group.
        for (Node &cc : g->children) {
          progeny.emplace_back(std::move(cc));
        }
        g->children.clear();
      } else {
        // Whole node.
        progeny.emplace_back(std::move(node));
      }

    } else {
      progeny.emplace_back(std::move(node));
    }
  }
  children.clear();

  // No need for unstyled singleton nodes.
  // PERF: Even better would be to merge the parent and child style.
  bool no_style = !style.has_value() || IsDefault(style.value());
  if (progeny.size() == 1 && no_style) {
    return std::move(progeny[0]);
  }

  return SVG::Node{SVG::G{
      .style = style.has_value() ? std::move(style.value()) : Style{},
      .children = std::move(progeny),
    }};
}

// A Cubic Bézier approximating a 90-degree elliptical arc.
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
  // an arc (this is impossible with cubic Bézier). This is the
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
  std::optional<std::unordered_map<std::string, SVG::G>> doc_defs;

  std::optional<SVG::Style> RemoveStyleAttributes(XML::Node *node,
                                                  bool is_text) {
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

    if (auto so = GetStripAttribute("stroke-dasharray")) {
      had_style = true;
      if (so.value() == "none") {
        style.stroke_dasharray = {std::vector<double>()};
      } else {
        auto args = AllNumbers(so.value());
        if (!args.has_value() || args.value().empty()) {
          error = "stroke-dasharray needs at least one number or 'none'";
          return {};
        }

        style.stroke_dasharray = {args.value()};
      }
    }

    if (auto so = GetStripAttribute("stroke-dashoffset")) {
      had_style = true;
      if (auto co = ParseLength(so.value())) {
        style.stroke_width = co;
      } else {
        error = "Invalid length in stroke-offset";
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

    // Within <clipPath> the attribute is called clip-rule.
    // We just treat it the same way, which is a little sloppy,
    // but should be unambiguous for any reasonable SVG.
    if (auto fo = GetStripAttribute("clip-rule")) {
      if (fo.value() == "evenodd") {
        style.use_even_odd_rule = {true};
      } else if (fo.value() == "nonzero") {
        style.use_even_odd_rule = {false};
      }
    }

    if (auto po = GetStripAttribute("clip-path")) {
      had_style = true;
      std::string_view s(po.value());
      Util::RemoveOuterWhitespace(&s);
      if (Util::TryStripPrefix("url(#", &s) &&
          Util::TryStripSuffix(")", &s)) {
        if (doc_defs.has_value()) {
          if (doc_defs.value().contains(std::string(s))) {
            style.clip_path = {std::string(s)};
          } else {
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

    if (auto to = GetStripAttribute("transform")) {
      had_style = true;
      if (auto tfo = SVG::ParseTransformList(to.value())) {
        style.transform = tfo;
      } else {
        error = "Invalid transform list in transform attribute.";
        return {};
      }
    }

    if (auto fo = GetStripAttribute("font-family")) {
      had_style = true;
      // TODO: Should parse a comma inside quotes correctly.
      std::vector<std::string> ffs = Util::Split(fo.value(), ',');
      // For some reason, Illustrator will do like "Helvetica, Helvetica".
      std::unordered_set<std::string> already;
      std::vector<std::string> out;
      for (std::string &ff : ffs) {
        ff = Util::NormalizeWhitespace(ff);
        if (ff.size() >= 2 && ff[0] == ff.back() &&
            (ff[0] == '\'' || ff[0] == '\"')) {
          ff = ff.substr(1, ff.size() - 2);
        }

        if (!already.contains(ff)) {
          already.insert(ff);
          out.push_back(std::move(ff));
        }
      }
      if (out.empty()) {
        error = "There must be at least one font in font-family.";
        return {};
      }
      style.font_family = std::make_optional(std::move(out));
    }

    if (auto fo = GetStripAttribute("font-size")) {
      had_style = true;
      if (auto fso = ParseLength(fo.value())) {
        style.font_size = fso;
      } else {
        error = "Invalid length in font-size";
        return {};
      }
    }

    if (is_text) {
      if (auto xo = GetStripAttribute("x")) {
        had_style = true;
        double x = Util::ParseDouble(xo.value(), 0.0);
        if (!style.transform.has_value())
          style.transform = std::make_optional(IDENTITY_TRANSFORM);
        style.transform.value() =
          ComposeTransforms(style.transform.value(),
                            Transform{1.0, 0.0, 0.0, 1.0, x, 0.0});
      }
      if (auto yo = GetStripAttribute("y")) {
        had_style = true;
        double y = Util::ParseDouble(yo.value(), 0.0);
        if (!style.transform.has_value())
          style.transform = std::make_optional(IDENTITY_TRANSFORM);
        style.transform.value() =
          ComposeTransforms(style.transform.value(),
                            Transform{1.0, 0.0, 0.0, 1.0, 0.0, y});
      }
    }

    CHECK(!GetStripAttribute("text-anchor").has_value()) << "Sorry, "
      "text-anchor is not supported.";

    // Only if we actually saw style attributes.
    return had_style ? std::make_optional(style) : std::nullopt;
  }

  // Convert when inside a <text> node. Here, text XML nodes become
  // SVG text. Since text has its own "cursor" inside the transform,
  // we have to interpret the cursor to handle overrides correctly
  // and flatten this into pure transforms.
  SVG::Node ConvertTextRec(XML::Node *node,
                           double cursor_x, double cursor_y) {
    if (node->type == XML::NodeType::Text) {
      // We need to collapse whitespace like in HTML.
      std::string collapsed = Util::NormalizeWhitespace(node->contents);

      if (collapsed.empty()) {
        return SVG::Node{SVG::G{.style = {}, .children = {}}};
      }

      return SVG::Node{SVG::Text{.content = std::move(collapsed)}};
    }

    CHECK(node->type == XML::NodeType::Element);

    bool had_translate = false;
    auto GetTranslateAttr = [node, &had_translate](std::string_view a) ->
      std::optional<double> {
      if (auto it = node->attrs.find(std::string(a));
          it != node->attrs.end()) {
        had_translate = true;
        double v = Util::ParseDouble(it->second, 0.0);
        node->attrs.erase(it);
        return {v};
      }

      return std::nullopt;
    };

    std::optional<SVG::Style> maybe_style =
      RemoveStyleAttributes(node, true);
    if (!error.empty()) return {};

    double next_x = cursor_x, next_y = cursor_y;
    if (auto xo = GetTranslateAttr("x")) {
      next_x = xo.value();
    }
    if (auto yo = GetTranslateAttr("y")) {
      next_y = yo.value();
    }
    if (auto dxo = GetTranslateAttr("dx")) {
      next_x += dxo.value();
    }
    if (auto dyo = GetTranslateAttr("dy")) {
      next_y += dyo.value();
    }

    // Need to modify matrix for this element?
    if (had_translate) {
      // The transform's translation is relative.
      double dx = next_x - cursor_x;
      double dy = next_y - cursor_y;

      if (dx != 0.0 || dy != 0.0) {
        if (!maybe_style.has_value())
          maybe_style = {SVG::Style{}};
        SVG::Style &style = maybe_style.value();

        if (!style.transform.has_value())
          style.transform = std::make_optional(IDENTITY_TRANSFORM);
        style.transform.value() =
          ComposeTransforms(style.transform.value(),
                            Transform{1.0, 0.0, 0.0, 1.0, dx, dy});
      }
    }

    // Both "tspan" and "text" are just treated like a <g> here.
    // (We only expect to see text in the initial call but we're not
    // strict about it.)
    if (node->tag == "tspan" || node->tag == "text") {

      std::vector<SVG::Node> children;
      for (XML::Node &child : node->children) {
        // XXX: We really should be measuring the x-width of each
        // text span and advancing the cursor. This assumes that
        // all text is positioned absolutely on the <text> or <span>
        // nodes (which appears to be the case in illustrator).
        children.emplace_back(ConvertTextRec(&child, next_x, next_y));
      }

      return SVG::MakeGroup(std::move(maybe_style),
                            std::move(children));
    } else {
      Print("Looking at XML:\n{}\n", XML::DebugString(*node));
      error = "Inside <text>, we can only handle text nodes and <tspan>.";
      return {};
    }
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

      // We treat x= and y= as style attributes (transform) for
      // text nodes and their descendants.
      const bool is_text = tag == "text";
      std::optional<SVG::Style> maybe_style =
        RemoveStyleAttributes(node, is_text);
      if (!error.empty())
        return {};

      if (tag == "g") {
        SVG::G g;

        std::vector<SVG::Node> children;
        for (XML::Node &child : node->children) {
          children.emplace_back(ConvertRec(&child));
        }

        return SVG::MakeGroup(std::move(maybe_style),
                              std::move(children));

      } else if (maybe_style.has_value()) {
        // Otherwise, if there are style attributes, insert
        // a singleton group so that we have a place to
        // put them.
        //
        // We know that the recusion is well-founded because we must
        // have removed at least one attribute.
        return SVG::MakeGroup(maybe_style, {ConvertRec(node)});

      } else if (tag == "path") {
        // This is the main thing we're interested in, and we
        // transform various other tags into this.
        auto dit = node->attrs.find("d");
        if (dit == node->attrs.end()) {
          // No path data. A no-op.
          return SVG::Node(SVG::G());
        }

        const std::string &d = dit->second;

        if (std::optional<std::vector<SVG::PathCommand>> opathdata =
            SVG::InterpretPathData(d, &error)) {

          if (opathdata.value().empty()) {
            return SVG::Node(SVG::G());
          }

          return SVG::Node(SVG::Path{.data = std::move(opathdata.value())});

        } else {
          error = "Couldn't parse path data.";
          return {};
        }

      } else if (tag == "line") {
        double x1 = Util::ParseDouble(node->attrs["x1"], 0.0);
        double y1 = Util::ParseDouble(node->attrs["y1"], 0.0);
        double x2 = Util::ParseDouble(node->attrs["x2"], 0.0);
        double y2 = Util::ParseDouble(node->attrs["y2"], 0.0);

        std::string d = std::format("M {},{} L {},{}", x1, y1, x2, y2);
        node->tag = "path";
        node->attrs.erase("x1");
        node->attrs.erase("y1");
        node->attrs.erase("x2");
        node->attrs.erase("y2");
        node->attrs["d"] = std::move(d);
        return ConvertRec(node);

      } else if (tag == "polygon" || tag == "polyline") {
        auto pit = node->attrs.find("points");
        if (pit == node->attrs.end()) {
          return SVG::Node(SVG::G());
        }

        // Polyline and polygon are the same except that
        // the polygon closes the path.
        const char *z = tag == "polygon" ? " Z" : "";

        std::string d = std::format("M {}{}", pit->second, z);

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

      } else if (tag == "text") {
        // Attributes have already been handled, so this acts
        // as a <g> that processes the subtree in text mode.
        return ConvertTextRec(node, 0.0, 0.0);

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
                   std::unordered_map<std::string, SVG::G> *defs) {
    if (node->type == XML::NodeType::Element) {

      if (auto iit = node->attrs.find("id"); iit != node->attrs.end()) {
        const std::string id = iit->second;
        if (id.empty() || defs->contains(id)) {
          error = "Node id invalid or not unique";
          return false;
        }

        (*defs)[id] = ConvertUnrendered(node);
        return error.empty();

      } else {
        for (XML::Node &child : node->children) {
          if (!CollectDefs(&child, defs)) {
            return false;
          }
        }
      }
    }

    return true;
  }

  static void TransformPath(const Transform &tf,
                            std::vector<SVG::PathCommand> *cmds) {
    for (SVG::PathCommand &cmd : *cmds) {
      cmd = SVG::TransformCommand(tf, cmd);
    }
  }

  // Get all the paths from the node into a flat vector.
  // The boolean is true if that path uses even-odd rule.
  // Applies transform style, but ignores other style.
  static void FlattenPaths(const Transform &tf,
                           bool even_odd,
                           SVG::Node *node,
                           std::vector<std::pair<SVG::Path, bool>> *paths) {
    if (SVG::Path *p = std::get_if<SVG::Path>(&node->v)) {
      TransformPath(tf, &p->data);
      paths->push_back(std::make_pair(*p, even_odd));

    } else {
      SVG::G *g = std::get_if<SVG::G>(&node->v);
      CHECK(g != nullptr);
      even_odd = g->style.use_even_odd_rule.has_value() ?
        g->style.use_even_odd_rule.value() : even_odd;

      if (g->style.transform.has_value()) {
        Transform tfc = ComposeTransforms(tf, g->style.transform.value());
        for (SVG::Node &child : g->children) {
          FlattenPaths(tfc, even_odd, &child, paths);
        }
      } else {
        for (SVG::Node &child : g->children) {
          FlattenPaths(tf, even_odd, &child, paths);
        }
      }
    }
  }

  // A clipPath cannot contain <g>, so we need to bake in
  // attributes like transformations. We resolve this to
  // a single <g> node with <path> as immediate children.
  bool BakeClipPath(SVG::Node *node) {
    std::vector<std::pair<SVG::Path, bool>> paths;
    FlattenPaths(IDENTITY_TRANSFORM, false, node, &paths);

    std::optional<bool> even_odd;
    for (const auto &[_, eo] : paths) {
      if (even_odd.has_value()) {
        if (even_odd.value() != eo) {
          error = "A clipPath uses a mix of nonzero and even/odd "
            "fill; I don't know how to handle this.";
          return false;
        } else {
          even_odd = {eo};
        }
      }
    }

    // Now replace the node with a single <g> node that has the paths
    // as immediate children.
    SVG::G g;
    g.style.use_even_odd_rule = even_odd;
    g.children.reserve(paths.size());
    for (auto &[path, _] : paths) {
      g.children.emplace_back(std::move(path));
    }
    *node = SVG::Node{std::move(g)};

    return true;
  }

  SVG::G ConvertUnrendered(XML::Node *node) {
    std::string tag = node->tag;
    // In the future, this could use the tag type (e.g. clipPath)
    // to do more checking, etc.. But we just treat all of these
    // as transparent groups today.
    node->tag = "g";
    SVG::Node ret = ConvertRec(node);

    if (tag == "clipPath") {
      BakeClipPath(&ret);
    }

    // Always a G, so make a singleton if needed.
    if (SVG::G *g = std::get_if<SVG::G>(&ret.v)) {
      return *g;
    } else {
      return SVG::G{
        .style = {},
        .children = std::vector<SVG::Node>{std::move(ret)}
      };
    }
  }

  // Recursively find unrendered nodes that have ids, like <clipPath>
  // and anything within <defs>. Puts them in defs and deletes their
  // contents. Returns false and sets the error member if the SVG is
  // invalid.
  bool FindUnrenderedNodes(XML::Node *node,
                           std::unordered_map<std::string, SVG::G> *defs) {
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
      std::unordered_map<std::string, SVG::G> defs;
      if (!FindUnrenderedNodes(root, &defs)) {
        if (error.empty()) error = "Error parsing defs?";
        return;
      }

      // Now the defs are available for use.
      doc_defs = std::make_optional(std::move(defs));
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
  double last_cx = 0.0, last_cy = 0.0;
  // Lowercase version of last command. This is needed
  // because the rules about the last control point
  // only apply when the last command was of the same
  // type (c/s or q/t).
  char last_cmd = '_';
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

        auto [nx, ny] = a.value();
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

        px = nx;
        py = ny;
        last_cmd = cmd | 32;
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

        auto [nx, ny] = a.value();
        if (cmd == 'l') {
          nx += px;
          ny += py;
        }

        cmds.emplace_back(LineTo{.x = nx, .y = ny});
        px = nx;
        py = ny;
        last_cmd = cmd | 32;
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

        px = n;
        last_cmd = cmd | 32;
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

        py = n;
        last_cmd = cmd | 32;
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

        auto [x1, y1, x2, y2, x, y] = a.value();

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
        last_cmd = cmd | 32;
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

        auto [x2, y2, x, y] = a.value();

        if (cmd == 's') {
          x2 += px;
          y2 += py;
          x += px;
          y += py;
        }

        // Only retain the last control point if the last
        // command was the same curve type.
        if (!(last_cmd == 's' ||
              last_cmd == 'c')) {
          last_cx = px;
          last_cy = py;
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
        last_cmd = cmd | 32;
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

        auto [cx, cy, x, y] = a.value();

        if (cmd == 'q') {
          cx += px;
          cy += py;
          x += px;
          y += py;
        }

        cmds.push_back(QuadraticBezier(px, py, cx, cy, x, y));

        px = x;
        py = y;

        last_cx = cx;
        last_cy = cy;
        last_cmd = cmd | 32;
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

        auto [x, y] = a.value();

        if (cmd == 't') {
          x += px;
          y += py;
        }

        if (!(last_cmd == 'q' ||
              last_cmd == 't')) {
          last_cx = px;
          last_cy = py;
        }

        // Calculate reflection of previous control point.
        // e.g. we have v = (c - p)
        // and the reflected point is then p - v,
        // So p - (c - p) = 2p - c.
        double cx = (2.0 * px) - last_cx;
        double cy = (2.0 * py) - last_cy;

        cmds.push_back(QuadraticBezier(px, py, cx, cy, x, y));

        px = x;
        py = y;
        last_cx = cx;
        last_cy = cy;
        last_cmd = cmd | 32;
      }
      break;
    }

    case 'Z':
    case 'z':
      cmds.emplace_back(ClosePath{});
      // Move current position in case there are more commands.
      last_cx = px = startx;
      last_cy = py = starty;
      last_cmd = cmd | 32;
      break;

      // TODO: More commands here.
      // Arc is missing, but it is tricky and rare.

    default:
      Print(stderr, "Unimplemented path command {:c}\n", cmd);
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

SVG::PathCommand SVG::TransformCommand(const Transform &tf,
                                       const SVG::PathCommand &cmd) {
  auto Pt = [&tf](double x, double y) -> std::pair<double, double> {
    const auto &[a, b, c, d, e, f] = tf;
    return std::make_pair((a * x) + (c * y) + e,
                          (b * x) + (d * y) + f);
  };

  if (const SVG::MoveTo *m = std::get_if<SVG::MoveTo>(&cmd)) {
    const auto &[xx, yy] = Pt(m->x, m->y);
    return {SVG::MoveTo{.x = xx, .y = yy}};
  } else if (const SVG::LineTo *l = std::get_if<SVG::LineTo>(&cmd)) {
    const auto &[xx, yy] = Pt(l->x, l->y);
    return {SVG::LineTo{.x = xx, .y = yy}};

  } else if (const SVG::CubicBezier *c =
               std::get_if<SVG::CubicBezier>(&cmd)) {
    const auto &[cxx1, cyy1] = Pt(c->cx1, c->cy1);
    const auto &[cxx2, cyy2] = Pt(c->cx2, c->cy2);
    const auto &[xx, yy] = Pt(c->x, c->y);

    return {SVG::CubicBezier{
        .cx1 = cxx1, .cy1 = cyy1,
        .cx2 = cxx2, .cy2 = cyy2,
        .x = xx, .y = yy,
      }};

  } else if ([[maybe_unused]] const SVG::ClosePath *z =
               std::get_if<SVG::ClosePath>(&cmd)) {

    return {SVG::ClosePath{}};
  } else {

    LOG(FATAL) << "Bad variant?";
  }
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

  // Append a group, but using the specific tag name, and
  // with an id if supplied. Used internally by AppendSVG and
  // UseAs.
  void AppendGroup(int depth, std::string_view tag,
                   std::optional<std::string> id,
                   const SVG::G &g, std::string *out) {
    const SVG::Style &style = g.style;
    AppendFormat(out, "{}<{}", std::string(depth, ' '), tag);

    if (id.has_value()) {
      AppendFormat(out, " id=\"{}\"", id.value());
    }

    if (style.transform.has_value()) {
      const auto &[a, b, c, d, e, f] = style.transform.value();
      AppendFormat(out, " transform=\"matrix({:.11g} {:.11g} {:.11g} "
                   "{:.11g} {:.11g} {:.11g})\"", a, b, c, d, e, f);
    }

    auto ColorString = [](uint32_t c) -> std::string {
        if (c == SVG::COLOR_NONE) {
          return "none";
        } else {
          const auto &[r, g, b, a] = Unpack32(c);
          if (a == 0xFF) {
            return std::format("#{:02x}{:02x}{:02x}", r, g, b);
          } else {
            // Note: Illustrator does not seem to support rgba colors
            // for fill or stroke! rgb(255 255 0 / 0.5) might be a
            // good compromise since illustrator will treat that as
            // opaque yellow, but compliant parsers will treat it as
            // 50% transparent yellow. (BTW this may be technically
            // correct behavior in SVG 1.1?)
            //
            // try svgviewer.dev instead.
            return std::format("#{:08x}", c);
          }
        }
      };

    if (style.fill_color.has_value()) {
      AppendFormat(out, " fill=\"{}\"",
                   ColorString(style.fill_color.value()));
    }

    if (style.fill_opacity.has_value()) {
      AppendFormat(out, " fill-opacity=\"{}\"",
                   Rtos(style.fill_opacity.value()));
    }

    if (style.stroke_color.has_value()) {
      AppendFormat(out, " stroke=\"{}\"",
                   ColorString(style.stroke_color.value()));
    }

    if (style.stroke_opacity.has_value()) {
      AppendFormat(out, " stroke-opacity=\"{}\"",
                   Rtos(style.stroke_opacity.value()));
    }

    if (style.stroke_width.has_value()) {
      AppendFormat(out, " stroke-width=\"{}px\"",
                   Rtos(style.stroke_width.value()));
    }

    if (style.stroke_dasharray.has_value()) {
      const auto &a = style.stroke_dasharray.value();
      out->append(" stroke-dasharray=\"");
      for (size_t i = 0; i < a.size(); i++) {
        if (i != 0) out->append(" ");
        out->append(Rtos(a[i]));
      }
      out->append("\"");
    }

    if (style.stroke_dashoffset.has_value()) {
      AppendFormat(out, " stroke-dashoffset=\"{}\"",
                   Rtos(style.stroke_dashoffset.value()));
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

    if (style.font_family.has_value()) {
      AppendFormat(out, " font-family=\"{}\"",
                   Util::Join(style.font_family.value(), ", "));
    }

    if (style.font_size.has_value()) {
      AppendFormat(out, " font-size=\"{}\"", Rtos(style.font_size.value()));
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

    for (const auto &c : g.children) {
      AppendSVG(depth + 2, c, out);
    }
    AppendFormat(out, "{}</{}>\n", std::string(depth, ' '), tag);
  }


  void AppendSVG(int depth, const SVG::Node &node, std::string *out) {
    if (const SVG::G *g = std::get_if<SVG::G>(&node.v)) {
      AppendGroup(depth, "g", std::nullopt, *g, out);
    } else if (const SVG::Path *path = std::get_if<SVG::Path>(&node.v)) {
      std::string d = PathDataString(path->data);
      AppendFormat(out, "{}<path d=\"{}\" />\n", std::string(depth, ' '), d);
    } else if (const SVG::Text *text = std::get_if<SVG::Text>(&node.v)) {
      AppendFormat(out, "{}<text>{}</text>\n",
                   std::string(depth, ' '), text->content);
    } else {
      LOG(FATAL) << "Bad variant?";
   }
  }

  void UseAs(std::string_view tag_type, std::string_view idv) {
    std::string id(idv);
    auto it = doc.defs.find(id);
    CHECK(it != doc.defs.end()) << "Unresolved id: " << id;
    // Could check consistent use here.
    if (used_defs.contains(id)) return;
    // Always a group.
    const SVG::G &g = it->second;
    std::string rendered;
    AppendGroup(4, tag_type, {id}, g, &rendered);
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
