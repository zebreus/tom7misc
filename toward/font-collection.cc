
#include <array>
#include <cmath>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "ansi.h"
#include "arcfour.h"
#include "banned.h"
#include "base/print.h"
#include "bounds.h"
#include "color-util.h"
#include "font-db.h"
#include "fonts/ttf.h"
#include "level.h"
#include "randutil.h"
#include "svg.h"
#include "util.h"

static constexpr int CELLS_ACROSS = 12;
static constexpr int CELLS_DOWN = 8;

static constexpr int GRID_COLOR = 0x444444FF;

static std::optional<SVG::Path> LetterFromTTF(std::string_view filename,
                                              char c) {
  std::unique_ptr<TTF> ttf = TTF::Load(filename);
  if (ttf.get() == nullptr || ttf->FontInfo()->numGlyphs == 0) {
    return std::nullopt;
  }

  std::vector<TTF::Contour> contours = ttf->GetContours(static_cast<uint8_t>(c));
  if (contours.empty()) {
    return std::nullopt;
  }

  SVG::Path svg_path;
  for (const TTF::Contour& contour : contours) {
    if (contour.paths.empty()) continue;

    double current_x = contour.StartX();
    double current_y = contour.StartY();
    svg_path.data.push_back(SVG::MoveTo{current_x, current_y});

    for (const TTF::Path& p : contour.paths) {
      if (p.type == TTF::PathType::LINE) {
        svg_path.data.push_back(SVG::LineTo{p.x, p.y});
      } else if (p.type == TTF::PathType::BEZIER) {
        // Convert quadratic to cubic Bézier
        double cx1 = current_x + 2.0 / 3.0 * (p.cx - current_x);
        double cy1 = current_y + 2.0 / 3.0 * (p.cy - current_y);
        double cx2 = p.x + 2.0 / 3.0 * (p.cx - p.x);
        double cy2 = p.y + 2.0 / 3.0 * (p.cy - p.y);
        svg_path.data.push_back(
            SVG::CubicBezier{cx1, cy1, cx2, cy2, p.x, p.y});
      }
      current_x = p.x;
      current_y = p.y;
    }
    svg_path.data.push_back(SVG::ClosePath{});
  }

  return svg_path;
}


static void Generate() {
  ArcFour rc("generate");
  std::unique_ptr<FontDB> db = FontDB::Create("../fontdb/font-db.txt");

  std::vector<std::string> font_files;
  for (const auto &[filename, info] : db->Files()) {
    if (BannedFonts().contains(filename)) continue;

    const FontDB::Type t = info.type;
    if (t != FontDB::Type::SANS &&
        t != FontDB::Type::SERIF) continue;

    font_files.push_back(filename);
  }

  Shuffle(&rc, &font_files);

  SVG::Doc doc;
  float playfield_w = Levels::WIDTH;
  float playfield_h = Levels::HEIGHT;

  doc.view_box = std::array<double, 4>{
    0.0, 0.0, playfield_w * Levels::SVG_SCALE, playfield_h * Levels::SVG_SCALE,
  };

  SVG::G root_g;

  float cell_w = playfield_w / (float)CELLS_ACROSS;
  float cell_h = playfield_h / (float)CELLS_DOWN;

  auto AddRect = [&root_g](float cx, float cy, int blocks_w, int blocks_h,
                           uint32_t color) {
    SVG::Path path;
    float hw = blocks_w * Levels::BLOCK_SIZE * Levels::SVG_SCALE / 2.0f;
    float hh = blocks_h * Levels::BLOCK_SIZE * Levels::SVG_SCALE / 2.0f;
    cx *= Levels::SVG_SCALE;
    cy *= Levels::SVG_SCALE;

    path.data.push_back(SVG::MoveTo{cx - hw, cy - hh});
    path.data.push_back(SVG::LineTo{cx + hw, cy - hh});
    path.data.push_back(SVG::LineTo{cx + hw, cy + hh});
    path.data.push_back(SVG::LineTo{cx - hw, cy + hh});
    path.data.push_back(SVG::ClosePath{});

    SVG::Node path_node;
    path_node.v = std::move(path);

    SVG::G g;
    g.style.fill_color = color;
    g.style.stroke_color = SVG::COLOR_NONE;
    g.children.push_back(std::move(path_node));

    SVG::Node node;
    node.v = std::move(g);
    root_g.children.push_back(std::move(node));
  };

  // Horizontal separators (floors).
  for (int j = 1; j <= CELLS_DOWN; j++) {
    float x = playfield_w / 2.0f;
    float y = j * cell_h;
    AddRect(x, y, Levels::BLOCKS_ACROSS, 1, GRID_COLOR);
  }

  // Vertical separators (walls).
  for (int i = 0; i <= CELLS_ACROSS; i++) {
    float x = i * cell_w;
    float y = playfield_h / 2.0f;
    AddRect(x, y, 1, Levels::BLOCKS_DOWN + Levels::OUT_HEIGHT, GRID_COLOR);
  }

  auto font_it = font_files.begin();
  for (int j = 0; j < CELLS_DOWN; j++) {
    for (int i = 0; i < CELLS_ACROSS; i++) {
      bool placed = false;
      while (font_it != font_files.end()) {
        std::string_view font_file = *font_it++;
        std::optional<SVG::Path> opt_path = LetterFromTTF(font_file, 'S');
        if (!opt_path) {
          continue;
        }

        SVG::Path &path = opt_path.value();

        Bounds bounds;
        for (const auto &cmd : path.data) {
          if (const SVG::MoveTo *m = std::get_if<SVG::MoveTo>(&cmd)) {
            bounds.Bound(m->x, m->y);
          } else if (const SVG::LineTo *l = std::get_if<SVG::LineTo>(&cmd)) {
            bounds.Bound(l->x, l->y);
          } else if (const SVG::CubicBezier *c = std::get_if<SVG::CubicBezier>(&cmd)) {
            bounds.Bound(c->cx1, c->cy1);
            bounds.Bound(c->cx2, c->cy2);
            bounds.Bound(c->x, c->y);
          }
        }

        if (bounds.Empty() || bounds.Width() <= 0 || bounds.Height() <= 0) continue;

        // Check a range of angles to find the letter's true maximum extent,
        // which avoids taller or wider letters getting stuck.
        double max_extent = 0.0;
        for (int step = 0; step < 32; step++) {
          double angle = step * std::numbers::pi / 16.0;
          double cosa = std::cos(angle);
          double sina = std::sin(angle);
          Bounds rb;
          for (const auto &cmd : path.data) {
            auto add_pt = [&](double x, double y) {
              rb.Bound(x * cosa - y * sina, x * sina + y * cosa);
            };
            if (const SVG::MoveTo *m = std::get_if<SVG::MoveTo>(&cmd)) {
              add_pt(m->x, m->y);
            } else if (const SVG::LineTo *l = std::get_if<SVG::LineTo>(&cmd)) {
              add_pt(l->x, l->y);
            } else if (const SVG::CubicBezier *c = std::get_if<SVG::CubicBezier>(&cmd)) {
              add_pt(c->cx1, c->cy1);
              add_pt(c->cx2, c->cy2);
              add_pt(c->x, c->y);
            }
          }
          if (rb.Width() > max_extent) max_extent = rb.Width();
          if (rb.Height() > max_extent) max_extent = rb.Height();
        }

        if (max_extent <= 0.0) continue;

        double max_dim = cell_w < cell_h ? cell_w : cell_h;
        double max_diag = max_dim * 0.85;
        double scale = max_diag / max_extent;
        double target_w = bounds.Width() * scale;
        double target_h = bounds.Height() * scale;

        float x_offset = (cell_w - target_w) / 2.0f;
        float y_offset = (cell_h - target_h) / 2.0f;

        double final_cx = (i * cell_w + x_offset) * Levels::SVG_SCALE;
        double final_cy = (j * cell_h + y_offset) * Levels::SVG_SCALE;

        bounds.AddMarginFrac(0.07);
        Bounds::Scaler scaler = bounds.
          ScaleToFit(target_w, target_h).
          Zoom(Levels::SVG_SCALE, Levels::SVG_SCALE).
          PanScreen(final_cx, final_cy);

        for (auto &cmd : path.data) {
          if (SVG::MoveTo *m = std::get_if<SVG::MoveTo>(&cmd)) {
            m->x = scaler.ScaleX(m->x);
            m->y = scaler.ScaleY(m->y);
          } else if (SVG::LineTo *l = std::get_if<SVG::LineTo>(&cmd)) {
            l->x = scaler.ScaleX(l->x);
            l->y = scaler.ScaleY(l->y);
          } else if (SVG::CubicBezier *c = std::get_if<SVG::CubicBezier>(&cmd)) {
            c->cx1 = scaler.ScaleX(c->cx1);
            c->cy1 = scaler.ScaleY(c->cy1);
            c->cx2 = scaler.ScaleX(c->cx2);
            c->cy2 = scaler.ScaleY(c->cy2);
            c->x = scaler.ScaleX(c->x);
            c->y = scaler.ScaleY(c->y);
          }
        }

        SVG::Node path_node;
        path_node.v = std::move(path);

        float hue = rc.Byte() / 255.0f;
        uint32_t color = ColorUtil::HSVAToRGBA32(hue, 0.25f, 1.0f, 1.0f);

        SVG::G g;
        g.style.stroke_color = color;
        g.style.fill_color = SVG::COLOR_NONE;
        g.style.stroke_width = 2.0;
        g.children.push_back(std::move(path_node));

        SVG::Node node;
        node.v = std::move(g);
        root_g.children.push_back(std::move(node));

        placed = true;
        break;
      }

      if (!placed) {
        break;
      }
    }

    if (font_it == font_files.end()) {
      break;
    }
  }

  doc.root.v = std::move(root_g);

  std::string svg_str = SVG::ToSVG(doc);
  if (Util::WriteFile("letters.svg", svg_str)) {
    Print("Wrote letters.svg\n");
  } else {
    Print("Failed to write letters.svg\n");
  }
}

int main(int argc, char **argv) {
  ANSI::Init();

  Generate();

  return 0;
}
