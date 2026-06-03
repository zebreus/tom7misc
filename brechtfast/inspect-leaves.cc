
#include "albrecht.h"

#include <algorithm>
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
#include "base/logging.h"
#include "base/print.h"
#include "db.h"
#include "geom/polyhedra.h"
#include "make-svg.h"
#include "solve-leaf.h"
#include "svg.h"
#include "util.h"

using Aug = Albrecht::AugmentedPoly;

static void Inspect(std::string_view poly_name,
                    std::string_view filename,
                    SVGOptions svg_options) {
  auto [poly, example_net] = DB::GetPolyhedron(poly_name);

  Print("Polyhedron: {}\n", poly.name.empty() ? poly_name : poly.name);
  int num_faces = poly.faces->NumFaces();
  Print("  Vertices: {}\n", poly.faces->NumVertices());
  Print("  Edges:    {}\n", poly.faces->NumEdges());
  Print("  Faces:    {}\n", num_faces);

  CHECK(IsWellConditioned(poly.vertices));
  CHECK(IsManifold(poly));

  Aug aug = Aug(std::move(poly));

  std::vector<std::optional<SVG::Doc>> face_docs;
  for (int i = 0; i < num_faces; i++) {
    int edge_idx = aug.face_edges[i][0];
    auto net_opt = SolveLeaf::FindLeafUnfolding(aug, i, edge_idx);
    if (net_opt.has_value()) {
      SVG::Doc svg = MakeSVG::Make(
          aug, Albrecht::DebugUnfolding(aug, net_opt.value()), svg_options);
      SVG::RenameDefs(std::format("f{}-", i), &svg);
      face_docs.push_back(std::move(svg));
    } else {
      face_docs.push_back(std::nullopt);
    }
  }

  int best_cols = 1, best_rows = num_faces;
  double best_size = 0.0;
  for (int c = 1; c <= num_faces; c++) {
    int r = (num_faces + c - 1) / c;
    double size = std::min(1920.0 / c, 1080.0 / r);
    if (size > best_size) {
      best_size = size;
      best_cols = c;
      best_rows = r;
    }
  }

  SVG::Doc doc;
  doc.view_box = std::array<double, 4>{0, 0, 1920, 1080};

  SVG::G main_group;

  double margin = best_size * 0.05;
  double size = best_size - 2.0 * margin;

  double offset_x = (1920.0 - best_cols * best_size) / 2.0;
  double offset_y = (1080.0 - best_rows * best_size) / 2.0;

  for (size_t i = 0; i < face_docs.size(); i++) {
    if (!face_docs[i].has_value()) continue;

    auto &face_doc = face_docs[i].value();

    int c = i % best_cols;
    int r = i / best_cols;

    double bx = offset_x + c * best_size + margin;
    double by = offset_y + r * best_size + margin;

    CHECK(face_doc.view_box.has_value());
    const auto &view_box = face_doc.view_box.value();

    double doc_w = view_box[2];
    double doc_h = view_box[3];
    if (doc_w <= 0.0 || doc_h <= 0.0) {
      doc_w = 1024.0;
      doc_h = 1024.0;
    }

    double scale = std::min(size / doc_w, size / doc_h);
    double dx = bx - view_box[0] * scale + (size - doc_w * scale) / 2.0;
    double dy = by - view_box[1] * scale + (size - doc_h * scale) / 2.0;
    std::array<double, 6> transform = {scale, 0.0, 0.0, scale, dx, dy};

    SVG::G sub_group;
    sub_group.style.transform = transform;
    sub_group.children.push_back(std::move(face_doc.root));
    main_group.children.push_back(SVG::Node{std::move(sub_group)});

    for (auto &[id, def] : face_doc.defs) {
      doc.defs[id] = std::move(def);
    }
  }

  doc.root = SVG::Node{std::move(main_group)};
  std::string contents = SVG::ToSVG(doc);

  // Save the SVG to the named file.
  Util::WriteFile(filename, contents);
  Print("Wrote " AGREEN("{}") "\n", filename);
}

int main(int argc, char **argv) {
  ANSI::Init();

  std::string name;
  SVGOptions svg_options;
  svg_options.inserts = false;
  svg_options.edge_labels = false;
  svg_options.face_labels = true;

  for (int i = 1; i < argc; i++) {
    std::string_view arg = argv[i];
    if (arg == "-face-color" || arg == "-edge-color") {
      CHECK(i + 1 < argc) << arg << " needs an arg.";
      i++;
      auto c = Util::ParseHex(argv[i]);
      CHECK(c.has_value()) << arg << " must be a hex string like FF0000FF!";
      uint32_t cc = (uint32_t)c.value();
      if (arg == "-face-color") {
        svg_options.face_rgba = cc;
      } else {
        CHECK(arg == "-edge-color");
        svg_options.edge_rgba = cc;
      }
    } else if (arg == "-edge-stroke") {
      CHECK(i + 1 < argc) << arg << " needs an arg.";
      i++;
      auto w = Util::ParseDoubleOpt(argv[i]);
      CHECK(w.has_value()) << arg << " must be a float!";
      svg_options.edge_stroke = w.value();
    } else if (arg == "-inserts") {
      svg_options.inserts = true;
    } else if (arg == "-edge-labels") {
      svg_options.edge_labels = true;
    } else if (arg == "-no-face-labels") {
      svg_options.face_labels = false;
    } else {
      CHECK(name.empty()) << "Just one name.";
      name = arg;
    }
  }

  CHECK(!name.empty()) << "./inspect.exe "
                          "[-inserts] [-no-face-labels] "
                          "[-edge-labels] [-face-color hex] "
                          "[-edge-color rgba] [-edge-stroke width] name";

  Inspect(name, std::format("inspect-{}.svg", name), svg_options);

  return 0;
}
