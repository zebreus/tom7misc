

#include <cstdlib>
#include <format>
#include <optional>
#include <string>
#include <vector>
#include <string_view>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "db.h"
#include "geom/polyhedra.h"
#include "poly-util.h"
#include "util.h"

static void Export(int id, std::string_view filename) {
  DB db;

  DB::Hard hard = db.GetHard(id);

  std::optional<Polyhedron> opoly =
    PolyhedronFromConvexVertices(hard.poly_points);
  CHECK(opoly.has_value()) << "Bad poly points?";

  int nfaces = opoly.value().faces->NumFaces();
  int nedges = opoly.value().faces->NumEdges();
  int nverts = opoly.value().faces->NumVertices();

  SaveAsSTL(opoly.value(), filename);
  Print("Wrote " AWHITE("{}") " ({} f {} e {} v)\n",
        filename, nfaces, nedges, nverts);
}

int main(int argc, char **argv) {
  ANSI::Init();

  CHECK(argc >= 2) << "./tostl.exe hard-id [file.stl]";

  int id = atoi(argv[1]);
  CHECK(id > 0) << id;

  std::string filename = std::format("hard-{}.stl", id);
  if (argc >= 3) {
    filename = argv[2];
  }

  Export(id, filename);

  return 0;
}
