
#include "albrecht.h"

#include <cstdlib>
#include <ctime>
#include <format>
#include <optional>
#include <span>
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
#include "svg.h"
#include "union-find.h"
#include "util.h"

using Aug = Albrecht::AugmentedPoly;

static std::optional<Albrecht::DebugResult> MaybeNet(
    const Aug &aug,
    std::span<const int> edges) {
  const Faces &faces = *aug.poly.faces;
  const int num_faces = faces.NumFaces();
  const int num_edges = faces.NumEdges();

  BitString unfolding(num_edges, 0);
  UnionFind uf(num_faces);
  for (int i : edges) {
    const Faces::Edge &edge = faces.edges[i];
    if (uf.Find(edge.f0) != uf.Find(edge.f1)) {
      uf.Union(edge.f0, edge.f1);
      unfolding.Set(i, true);
    }
  }

  Albrecht::DebugResult dr =
    Albrecht::DebugUnfolding(aug, unfolding);
  if (dr.is_net) return {std::move(dr)};
  return std::nullopt;
}

// This assumes polyhedron's edges are somehow orderly,
// which is typically the case for the P/A/C ones.
static Albrecht::DebugResult GetNiceNet(const Aug &aug) {
  const Faces &faces = *aug.poly.faces;
  int num_edges = faces.NumEdges();

  ArcFour rc(std::format("tosvg.{}", time(nullptr)));

  BitString unfolding(num_edges, false);

  {
    std::vector<int> edges_twice;
    edges_twice.reserve(num_edges * 2);
    for (int i = 0; i < num_edges; i++) edges_twice.push_back(i);
    for (int i = 0; i < num_edges; i++) edges_twice.push_back(i);

    for (int offset = 0; offset < num_edges; offset++) {
      std::span<const int> edges(edges_twice.data() + offset, num_edges);
      if (auto dro = MaybeNet(aug, edges)) {
        Print("Good with offset {}\n", offset);
        return dro.value();
      }
    }
  }

  LOG(FATAL) << "Need some new heuristics!";
}

static std::optional<Polyhedron> PolyByName(std::string_view name) {
  auto po = PolyhedronByName(name);
  if (po.has_value()) return po.value();

  int id = Util::ParseInt64(name, -1);

  DB db;
  DB::Hard hard = db.GetHard(id);

  return PolyhedronFromConvexVertices(hard.poly_points);
}

static void ToSVG(std::string_view name, std::string_view filename) {
  std::optional<Polyhedron> opoly = PolyByName(name);
  CHECK(opoly.has_value()) << "Unknown polyhedron " << name;

  CHECK(IsWellConditioned(opoly.value().vertices));
  CHECK(IsManifold(opoly.value()));

  Aug aug = Aug(std::move(opoly.value()));

  Albrecht::DebugResult debug_result = GetNiceNet(aug);
  // XXX make these options
  SVG::Doc doc = Albrecht::MakeSVG(aug, debug_result, false, false);
  std::string contents = SVG::ToSVG(doc);

  // Save the SVG to the named file.
  Util::WriteFile(filename, contents);
  Print("Wrote " AGREEN("{}") "\n", filename);
}

int main(int argc, char **argv) {
  ANSI::Init();

  CHECK(argc == 2) << "./tosvg.exe id\n"
    "\n"
    "Give the id from the database or the polyhedron's name.";

  std::string_view name = argv[1];
  ToSVG(name, std::format("tosvg-{}.svg", name));

  return 0;
}
