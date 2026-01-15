
#include "svg.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "base/logging.h"
#include "base/print.h"
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


// Parse a color like "#fff" or "#123456".
static std::optional<uint32_t> ParseColor(std::string_view s) {
  Util::RemoveOuterWhitespace(&s);
  if (s.empty()) return {};
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
    } else if (s.size() == 6) {
      // #RRGGBB
      uint8_t r = (Util::HexDigitValue(s[0]) << 4) | Util::HexDigitValue(s[1]);
      uint8_t g = (Util::HexDigitValue(s[2]) << 4) | Util::HexDigitValue(s[3]);
      uint8_t b = (Util::HexDigitValue(s[4]) << 4) | Util::HexDigitValue(s[5]);
      return {Pack32(r, g, b, 0xFF)};
    } else {
      // Fall through...
    }
  }

  Print(stderr, "Unimplemented/invalid color attr: {}", s);
  return {};
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
  if (c == '-' || c == '.' || (c >= '0' && c <= '9')) {
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


struct Converter {
  SVG::Doc doc;
  std::string error;

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
      // XXX: Technically ';' can appear inside some css values,
      // like a quoted string. We could do like "split respecting
      // strings and parens."
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

    if (auto so = GetStripAttribute("stroke")) {
      had_style = true;
      if (auto co = ParseColor(so.value())) {
        style.stroke_color = co;
      } else {
        error = "Invalid color in stroke";
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

    // Only if we actually saw style attributes.
    return had_style ? std::make_optional(style) : std::nullopt;
  }

  // Consumes the XML tree.
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
      if (tag == "g") {
        SVG::G g;
        if (maybe_style.has_value()) {
          g.style = maybe_style.value();
        }

        for (XML::Node &child : node->children) {
          g.children.emplace_back(ConvertRec(&child));
        }
        return SVG::Node(std::move(g));

      } else if (maybe_style.has_value()) {
        // Otherwise, if there are style attributes, insert
        // a singleton group so that we have a place to
        // put them.
        SVG::G g;
        g.style = maybe_style.value();
        // We know that the recusion is well-founded because we must
        // have removed at least one attribute.
        g.children = {ConvertRec(node)};
        return SVG::Node(std::move(g));

      } else {

        LOG(FATAL) << "Unimplemented tag: " << tag;
      }

      // Elements
      // std::string tag;
      // std::unordered_map<std::string, std::string> attrs;
      // std::vector<Node> children;

    }

    LOG(FATAL) << "Unimplemented";
    return {};
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

      root->tag = "g";
      doc.root = ConvertRec(root);
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
  return converter.doc;
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
  // XXX will also need "last control point"
  double px = 0.0, py = 0.0;
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

        double n1, n2;
        std::tie(n1, n2) = a.value();
        if (cmd == 'm') {
          n1 += px;
          n2 += py;
        }

        // MoveTo has a special case for repeated arguments,
        // since moving multiple times would have no point.
        if (first) {
          cmds.emplace_back(MoveTo{.x = n1, .y = n2});
          first = false;
        } else {
          cmds.emplace_back(LineTo{.x = n1, .y = n2});
        }

        px = n1;
        py = n2;
      }
      break;
    }

      // TODO: More commands here.

    default:
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
