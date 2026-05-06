
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
#include "bit-string.h"
#include "db.h"
#include "geom/polyhedra.h"
#include "periodically.h"
#include "randutil.h"
#include "status-bar.h"
#include "svg.h"
#include "union-find.h"
#include "util.h"

using Aug = Albrecht::AugmentedPoly;

static void Inspect(int id, std::string_view filename) {
  DB db;
  DB::Hard hard = db.GetHard(id);

  std::optional<Polyhedron> opoly =
    PolyhedronFromConvexVertices(hard.poly_points);
  CHECK(opoly.has_value());

  CHECK(IsWellConditioned(opoly.value().vertices));
  CHECK(IsManifold(opoly.value()));

  Aug aug = Aug(std::move(opoly.value()));

  std::string contents;

  const Faces &faces = *aug.poly.faces;
  int num_faces = faces.NumFaces();
  int num_edges = faces.NumEdges();

  std::vector<Albrecht::DebugResult> non_nets;
  std::vector<BitString> seen_non_nets;
  std::vector<Albrecht::DebugResult> nets;

  auto AlreadySaw = [&](const BitString &unfolding) {
      for (const BitString &seen : seen_non_nets) {
        if (seen == unfolding) {
          return true;
        }
      }
      return false;
    };

  int attempts = 0;

  ArcFour rc(std::format("inspect.{}", time(nullptr)));

  StatusBar status(1);
  Periodically status_per(1.0);
  while ((non_nets.size() < 3 || nets.empty()) && attempts < 10000) {
    attempts++;
    BitString unfolding(num_edges, false);
    std::vector<int> edges(num_edges);
    for (int i = 0; i < num_edges; ++i) edges[i] = i;
    Shuffle(&rc, &edges);

    UnionFind uf(num_faces);
    for (int i : edges) {
      const Faces::Edge &edge = faces.edges[i];
      if (uf.Find(edge.f0) != uf.Find(edge.f1)) {
        uf.Union(edge.f0, edge.f1);
        unfolding.Set(i, true);
      }
    }

    if (Albrecht::IsNet(aug, unfolding)) {
      if (nets.empty()) {
        nets.push_back(Albrecht::DebugUnfolding(aug, unfolding));
      }
    } else {
      if (non_nets.size() < 3) {

        bool duplicate = AlreadySaw(unfolding);

        if (!duplicate) {
          seen_non_nets.push_back(unfolding);
          non_nets.push_back(Albrecht::DebugUnfolding(aug, unfolding));
        }
      }
    }

    status_per.RunIf([&]{
        status.Status("{} attempts, {} net(s), {} non-net(s)\n",
                      attempts, nets.size(), non_nets.size());
      });
  }

  status.Remove();
  Print("In {} attempts, Got {} net(s) and {} non-net(s).\n",
        attempts, nets.size(), non_nets.size());


  std::vector<SVG::Doc> quadrant_docs;
  for (size_t i = 0; i < non_nets.size() && i < 3; ++i) {
    SVG::Doc svg = Albrecht::MakeSVG(aug, non_nets[i]);
    SVG::RenameDefs(std::format("q{}-", i), &svg);
    quadrant_docs.push_back(std::move(svg));
  }

  if (!nets.empty()) {
    SVG::Doc svg = Albrecht::MakeSVG(aug, nets[0]);
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

  CHECK(argc == 2) << "./inspect.exe id";

  int id = atoi(argv[1]);
  CHECK(id > 0) << argv[1];

  Inspect(id, std::format("inspect-{}.svg", id));

  return 0;
}
