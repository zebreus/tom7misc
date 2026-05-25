
#include "albrecht.h"

#include <array>
#include <cstdlib>
#include <ctime>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ansi.h"
#include "arcfour.h"
#include "base/logging.h"
#include "base/print.h"
#include "db.h"
#include "examples.h"
#include "geom/polyhedra.h"
#include "svg.h"
#include "util.h"

using Aug = Albrecht::AugmentedPoly;

static void Inspect(std::string_view poly_name,
                    std::optional<int> face_idx,
                    std::optional<int> edge_idx,
                    std::string_view filename) {
  auto [poly, example_net] = DB::GetPolyhedron(poly_name);

  CHECK(IsWellConditioned(poly.vertices));
  CHECK(IsManifold(poly));

  Aug aug = Aug(std::move(poly));

  std::string contents;

  ArcFour rc(std::format("inspect.{}", time(nullptr)));

  static constexpr int TARGET_NON_NETS = 3;

  Examples examples = GetSomeExamples(&rc, aug,
                                      face_idx, edge_idx,
                                      example_net,
                                      1, TARGET_NON_NETS, true);

  std::vector<SVG::Doc> quadrant_docs;
  for (size_t i = 0; i < examples.non_nets.size() && i < 3; ++i) {
    SVG::Doc svg = Albrecht::MakeSVG(aug, examples.non_nets[i]);
    SVG::RenameDefs(std::format("q{}-", i), &svg);
    quadrant_docs.push_back(std::move(svg));
  }

  if (!examples.nets.empty()) {
    SVG::Doc svg = Albrecht::MakeSVG(aug, examples.nets[0]);
    SVG::RenameDefs("q3-", &svg);
    quadrant_docs.push_back(std::move(svg));
  }

  SVG::Doc doc;
  doc.view_box = std::array<double, 4>{0, 0, 2048, 2048};

  SVG::G main_group;

  double margin = 16.0;
  for (size_t i = 0; i < quadrant_docs.size(); i++) {
    double tx = (i & 1) * 1024.0;
    double ty = (i >> 1) * 1024.0;

    double bx = tx + ((i & 1) ? margin / 2.0 : margin);
    double by = ty + ((i >> 1) ? margin / 2.0 : margin);
    double size = 1024.0 - 1.5 * margin;

    SVG::Path bg_rect;
    bg_rect.data = {
      SVG::MoveTo{bx, by},
      SVG::LineTo{bx + size, by},
      SVG::LineTo{bx + size, by + size},
      SVG::LineTo{bx, by + size},
      SVG::ClosePath{}
    };

    SVG::G rect_group;
    rect_group.style.fill_color = SVG::COLOR_NONE;
    rect_group.style.stroke_color = 0x000000FF;
    rect_group.style.stroke_width = 2.0;
    rect_group.children.push_back(SVG::Node{std::move(bg_rect)});
    main_group.children.push_back(SVG::Node{std::move(rect_group)});

    double s = size / 1024.0;
    std::array<double, 6> transform = {s, 0.0, 0.0, s, bx, by};

    SVG::G sub_group;
    sub_group.style.transform = transform;
    sub_group.children.push_back(std::move(quadrant_docs[i].root));
    main_group.children.push_back(SVG::Node{std::move(sub_group)});

    // Don't need to transform the clip paths, because SVG semantics
    // will move the clip path to the coordinate system of the element
    // it's used on!
    for (auto &[id, def] : quadrant_docs[i].defs) {
      doc.defs[id] = std::move(def);
    }
  }

  doc.root = SVG::Node{std::move(main_group)};
  contents = SVG::ToSVG(doc);

  // Save the SVG to the named file.
  Util::WriteFile(filename, contents);
  Print("Wrote " AGREEN("{}") "\n", filename);
}

int main(int argc, char **argv) {
  ANSI::Init();

  std::string name;
  std::optional<int> face_idx, edge_idx;
  for (int i = 1; i < argc; i++) {
    std::string_view arg = argv[i];
    if (arg == "-face" || arg == "-edge") {
      CHECK(i + 1 < argc) << "-face and -edge need arg.";
      i++;
      std::optional<int64_t> of = Util::ParseDoubleOpt(argv[i]);
      CHECK(of.has_value()) << "-face and -edge must be a number!";
      if (arg == "-face") face_idx = {of.value()};
      else if (arg == "-edge") edge_idx = {of.value()};
    } else {
      CHECK(name.empty()) << "Just one name.";
      name = arg;
    }
  }

  CHECK(!name.empty()) << "./inspect.exe [-face idx] [-edge idx] name";

  Inspect(name, face_idx, edge_idx,
          std::format("inspect-{}.svg", name));

  return 0;
}
