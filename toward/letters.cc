
#include "letters.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "fonts/ttf.h"
#include "geom/polygonization.h"
#include "lines.h"

std::unique_ptr<Letters> Letters::LoadFont(std::string_view filename) {
  std::unique_ptr<Letters> result = std::make_unique<Letters>();

  TTF ttf(filename);
  if (!ttf.FontInfo() || ttf.FontInfo()->numGlyphs == 0) {
    return {nullptr};
  }

  // Printable ascii for now.
  for (uint32_t codepoint = 32; codepoint < 127; codepoint++) {
    std::vector<TTF::Contour> contours = ttf.GetContours(codepoint);

    if (contours.empty()) {
      // Space or missing character. We still want an entry for it.
      if (codepoint == ' ') {
        result->letter[codepoint] = Letter{};
      }
      continue;
    }

    Polygonization::Shape shape;
    for (const TTF::Contour &contour : contours) {
      if (contour.paths.empty()) continue;

      std::vector<Polygonization::vec2> pts;
      double cur_x = contour.StartX();
      double cur_y = contour.StartY();

      for (const TTF::Path &p : contour.paths) {
        if (p.type == TTF::PathType::LINE) {
          pts.push_back({(double)p.x, (double)p.y});
          cur_x = p.x;
          cur_y = p.y;
        } else if (p.type == TTF::PathType::BEZIER) {
          // Nominally the glyphs are in the unit square.
          static constexpr double MAX_ERR_SQUARED = 0.001 * 0.001;
          auto points = TesselateQuadraticBezier<double>(
              cur_x, cur_y, p.cx, p.cy, p.x, p.y, MAX_ERR_SQUARED);
          for (const auto &pt : points) {
            pts.push_back({pt.first, pt.second});
          }
          cur_x = p.x;
          cur_y = p.y;
        }
      }

      shape.polys.emplace_back(std::move(pts));
    }

    // Polygonize the shape into convex polygons.
    // 8 is Box2D's max.
    auto poly_result = Polygonization::Polygonize(shape, 8);
    if (std::holds_alternative<Polygonization::Mesh>(poly_result)) {
      Letter letter;
      letter.mesh = std::get<Polygonization::Mesh>(std::move(poly_result));
      result->letter[codepoint] = std::move(letter);
    } else {
      LOG(FATAL) << std::get<std::string_view>(poly_result);
    }
  }

  for (const auto& kv1 : result->letter) {
    uint32_t c1 = kv1.first;
    for (const auto& kv2 : result->letter) {
      uint32_t c2 = kv2.first;
      result->kerning[KernKey(c1, c2)] = ttf.NormKernAdvance(c1, c2);
    }
  }

  return result;
}


