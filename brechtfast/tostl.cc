

#include <cstdlib>
#include <format>
#include <string>
#include <string_view>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "db.h"
#include "geom/polyhedra.h"
#include "poly-util.h"

static void Export(std::string_view name, std::string_view filename) {

  const auto &[poly, example_net] = DB::GetPolyhedron(name);

  int nfaces = poly.faces->NumFaces();
  int nedges = poly.faces->NumEdges();
  int nverts = poly.faces->NumVertices();

  SaveAsSTL(poly, filename);
  Print("Wrote " AWHITE("{}") " ({} f {} e {} v)\n",
        filename, nfaces, nedges, nverts);
}

int main(int argc, char **argv) {
  ANSI::Init();

  CHECK(argc >= 2) << "./tostl.exe polyname [file.stl]\n\n"
    "polyname can be a number (hard database id), j24,\n"
    "dodecahedron, rubikscube, etc.";

  std::string_view name = argv[1];
  CHECK(!name.empty());

  std::string prefix =
    atoi(argv[1]) > 0 ? std::format("hard-{}", name) : std::string(name);

  std::string filename = std::format("{}.stl", prefix);
  if (argc >= 3) {
    filename = argv[2];
  }

  Export(name, filename);

  return 0;
}
