
#include "letters.h"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "geom/polygonization.h"
#include "geom/polygons.h"
#include "image.h"

static void MakeFontSheet(std::string_view font_filename,
                          std::string_view image_filename) {
  std::optional<Letters> oletters = Letters::LoadFont(font_filename);
  CHECK(oletters.has_value()) << font_filename;
  const Letters &letters = oletters.value();

  ImageRGBA sheet(3840, 2160);
  sheet.Clear32(0x000000FF);

  int cols = 7;
  int rows = 4;
  int cell_w = sheet.Width() / cols;
  int cell_h = sheet.Height() / rows;

  // Use a scale that leaves a comfortable margin.
  double scale = std::min(cell_w, cell_h) * 0.8;
  double margin_x = (cell_w - scale) / 2.0;
  double margin_y = (cell_h - scale) / 2.0;

  for (char c = 'A'; c <= 'Z'; ++c) {
    auto it = letters.letter.find(c);
    if (it == letters.letter.end()) continue;

    const Letter& letter = it->second;
    int idx = c - 'A';
    int col = idx % cols;
    int row = idx / cols;

    double px = col * cell_w + margin_x;
    double py = row * cell_h + margin_y;

    // Optional label for the cell
    std::string label = std::string(1, c) + " (" + std::to_string(letter.mesh.polygons.size()) + ")";
    sheet.BlendText2x32(px, py - 24, 0x888888FF, label);

    for (const auto& poly : letter.mesh.polygons) {
      // Draw filled convex polygon (triangle fan from poly[0])
      for (size_t i = 1; i + 1 < poly.size(); ++i) {
        auto [x0, y0] = letter.mesh.vertices[poly[0]];
        auto [x1, y1] = letter.mesh.vertices[poly[i]];
        auto [x2, y2] = letter.mesh.vertices[poly[i + 1]];

        int tx0 = px + x0 * scale;
        int ty0 = py + y0 * scale;
        int tx1 = px + x1 * scale;
        int ty1 = py + y1 * scale;
        int tx2 = px + x2 * scale;
        int ty2 = py + y2 * scale;

        // Dark grey fill
        sheet.BlendTriangle32(tx0, ty0, tx1, ty1, tx2, ty2, 0x444444FF);
      }

      // Draw white outline for visual inspection
      for (size_t i = 0; i < poly.size(); ++i) {
        auto [x1, y1] = letter.mesh.vertices[poly[i]];
        auto [x2, y2] = letter.mesh.vertices[poly[(i + 1) % poly.size()]];

        int ix1 = px + x1 * scale;
        int iy1 = py + y1 * scale;
        int ix2 = px + x2 * scale;
        int iy2 = py + y2 * scale;

        sheet.BlendLine32(ix1, iy1, ix2, iy2, 0xFFFFFFFF);
      }
    }

    // Draw red points at mesh vertices
    for (const auto& pt : letter.mesh.vertices) {
      auto [x, y] = pt;
      int vx = px + x * scale;
      int vy = py + y * scale;
      sheet.SetPixel32(vx, vy, 0xFF0000FF);
    }
  }

  sheet.Save(image_filename);
}


int main(int argc, char **argv) {
  ANSI::Init();

  MakeFontSheet("helvetica.ttf", "helvetica.png");

  Print("OK\n");
  return 0;
}
