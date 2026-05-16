
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
#include "geom/johnson-solids.h"
#include "geom/polyhedra.h"
#include "nasty.h"
#include "periodically.h"
#include "randutil.h"
#include "solve-leaf.h"
#include "status-bar.h"
#include "svg.h"
#include "union-find.h"
#include "util.h"

using Aug = Albrecht::AugmentedPoly;

static Polyhedron GetPolyhedron(std::string_view name) {
  {
    std::string_view johnson = name;
    if (Util::TryStripPrefix("j", &johnson)) {
      int64_t i = Util::ParseInt64(johnson);
      if (i >= 1 && i <= 92) return JohnsonSolid(i);
    }
  }

  if (auto opoly = Nasty::ByName(name)) {
    return opoly.value();
  } else if (auto opoly = PolyhedronByName(name)) {
    return opoly.value();
  } else {
    int64_t id = Util::ParseInt64(name);
    if (id > 0) {
      DB db;
      DB::Hard hard = db.GetHard(id);

      std::optional<Polyhedron> opoly =
        PolyhedronFromConvexVertices(hard.poly_points);
      CHECK(opoly.has_value()) << name;
      return opoly.value();
    }
  }

  LOG(FATAL) << "Unknown polyhedron " << name;
}

static BitString Sample(ArcFour *rc, const Aug &aug,
                        std::optional<int> face_idx,
                        bool want_net, bool want_non_net) {
  const Faces &faces = *aug.poly.faces;
  int num_faces = faces.NumFaces();
  int num_edges = faces.NumEdges();

  if (want_non_net && rc->Byte() > 200) {
    // If we still want non-nets, most of the time we'll try a mostly
    // depth-first approach. This tends to produce longer chains of
    // faces, which have a higher chance of self-intersection,
    // compared to the bushy graphs produced by Kruskal's algorithm
    // below.

    int root = face_idx.value_or(RandTo(rc, num_faces));
    BitString unfolding(num_edges, false);
    BitString visited(num_faces, false);
    std::vector<int> stack = {root};
    visited.Set(root, true);

    while (!stack.empty()) {
      int cur = stack.back();

      std::vector<int> candidates;
      for (int e : aug.face_edges[cur]) {
        int next_face =
          (faces.edges[e].f0 == cur) ? faces.edges[e].f1 : faces.edges[e].f0;
        if (!visited[next_face]) {
          candidates.push_back(e);
        }
      }

      if (candidates.empty()) {
        stack.pop_back();
      } else {
        int e = candidates[RandTo(rc, candidates.size())];
        int next_face =
          (faces.edges[e].f0 == cur) ? faces.edges[e].f1 : faces.edges[e].f0;
        visited.Set(next_face, true);
        unfolding.Set(e, true);

        if (face_idx.has_value() && cur == root) {
          // Prevent the root from gaining additional children by removing
          // it from the stack, forcing it to be a leaf.
          stack.pop_back();
        }

        stack.push_back(next_face);
      }
    }

    return unfolding;
  }

  // If we have a face_idx, use the leaf solver to sample.
  if (face_idx.has_value()) {
    return SolveLeaf::SampleFace(rc, aug, face_idx.value());
  }

  BitString unfolding(num_edges, false);
  std::vector<int> edges(num_edges);
  for (int i = 0; i < num_edges; ++i) edges[i] = i;
  Shuffle(rc, &edges);

  UnionFind uf(num_faces);
  for (int i : edges) {
    const Faces::Edge &edge = faces.edges[i];
    if (uf.Find(edge.f0) != uf.Find(edge.f1)) {
      uf.Union(edge.f0, edge.f1);
      unfolding.Set(i, true);
    }
  }

  return unfolding;
}

static void Inspect(std::string_view poly_name,
                    std::optional<int> face_idx,
                    std::optional<int> edge_idx,
                    std::string_view filename) {
  Polyhedron poly = GetPolyhedron(poly_name);

  CHECK(IsWellConditioned(poly.vertices));
  CHECK(IsManifold(poly));

  Aug aug = Aug(std::move(poly));

  std::string contents;

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
  static constexpr int TARGET_NON_NETS = 3;
  while ((non_nets.size() < TARGET_NON_NETS || nets.empty()) && attempts < 500000) {
    attempts++;
    BitString unfolding = Sample(&rc, aug, face_idx, nets.empty(),
                                 non_nets.size() < TARGET_NON_NETS);

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
