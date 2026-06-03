
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
#include "make-svg.h"
#include "svg.h"
#include "util.h"

using Aug = Albrecht::AugmentedPoly;

static void Inspect(std::string_view poly_name,
                    Constraint constraint,
                    std::string_view filename,
                    SVGOptions svg_options) {
  auto [poly, example_net] = DB::GetPolyhedron(poly_name);

  Print("Polyhedron: {}\n", poly.name.empty() ? poly_name : poly.name);
  Print("  Vertices: {}\n", poly.faces->NumVertices());
  Print("  Edges:    {}\n", poly.faces->NumEdges());
  Print("  Faces:    {}\n", poly.faces->NumFaces());

  CHECK(IsWellConditioned(poly.vertices));
  CHECK(IsManifold(poly));

  Aug aug = Aug(std::move(poly));

  std::string contents;

  ArcFour rc(std::format("inspect.{}", time(nullptr)));

  static constexpr int TARGET_NON_NETS = 3;

  Examples examples = GetSomeExamples(&rc, aug,
                                      constraint,
                                      example_net,
                                      1, TARGET_NON_NETS, true);

  std::vector<SVG::Doc> quadrant_docs;
  for (size_t i = 0; i < examples.non_nets.size() && i < 3; ++i) {
    SVG::Doc svg = MakeSVG::Make(aug, examples.non_nets[i],
                                 svg_options);
    SVG::RenameDefs(std::format("q{}-", i), &svg);
    quadrant_docs.push_back(std::move(svg));
  }

  if (!examples.nets.empty()) {
    SVG::Doc svg = MakeSVG::Make(aug, examples.nets[0],
                                 svg_options);
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

  std::vector<std::string> args(argv + 1, argv + argc);
  const Constraint constraint = ParseConstraints(&args);

  std::string name;
  SVGOptions svg_options;
  for (size_t i = 0; i < args.size(); i++) {
    std::string_view arg = args[i];
    if (arg == "-no-inserts") {
      svg_options.inserts = false;
    } else if (arg == "-no-face-labels") {
      svg_options.face_labels = false;
    } else if (arg == "-no-edge-labels") {
      svg_options.edge_labels = false;
    } else if (arg == "-face-color" || arg == "-edge-color") {
      CHECK(i + 1 < args.size()) << arg << " needs an arg.";
      i++;
      auto c = Util::ParseHex(args[i]);
      CHECK(c.has_value()) << arg << " must be a hex string like FF0000FF!";
      uint32_t cc = (uint32_t)c.value();
      if (arg == "-face-color") {
        svg_options.face_rgba = cc;
      } else {
        CHECK(arg == "-edge-color");
        svg_options.edge_rgba = cc;
      }
    } else if (arg == "-edge-stroke") {
      CHECK(i + 1 < args.size()) << arg << " needs an arg.";
      i++;
      auto w = Util::ParseDoubleOpt(args[i]);
      CHECK(w.has_value()) << arg << " must be a float!";
      svg_options.edge_stroke = w.value();
    } else {
      CHECK(name.empty()) << "Just one name.";
      name = arg;
    }
  }

  CHECK(!name.empty()) << "./inspect.exe [-hull f e] [-line] "
                          "[-leaf f e] [-leaf-face f] [-dual-leaf e] "
                          "[-no-inserts] [-no-face-labels] "
                          "[-no-edge-labels] [-face-color hex] "
                          "[-edge-color rgba] [-edge-stroke width] name";

  Inspect(name, constraint,
          std::format("inspect-{}.svg", name),
          svg_options);

  return 0;
}
