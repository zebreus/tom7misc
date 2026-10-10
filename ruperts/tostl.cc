
#include "ansi.h"

#include <cstdlib>
#include <format>
#include <string>
#include <string_view>

#include "base/print.h"
#include "geom/polyhedra.h"
#include "ruperts-util.h"
#include "solutions.h"
#include "yocto-math.h"

using frame3 = yocto::frame<double, 3>;
using Nopert = SolutionDB::Nopert;

static constexpr bool EXACT = true;

static void Save(bool dualize, bool normalize, std::string_view name, std::string_view file) {
  SolutionDB db;
  Polyhedron p = db.AnyPolyhedronByName(name);
  if (dualize) p = DualizePoly(p);
  if (normalize) p = NormalizeRadius(p);

  TriangularMesh3D mesh = PolyToTriangularMesh(p);
  OrientMesh(&mesh);
  SaveAsSTL(mesh, file, name, false, EXACT);
}

static void Usage() {
  Print("./tostl.exe [-dual] [-no-normalize] polyhedronname [output.stl]\n");
}

int main(int argc, char **argv) {
  ANSI::Init();

  std::string name;
  std::string file;

  bool dualize = false;
  bool normalize = true;
  for (int i = 1; i < argc; i++) {
    std::string_view arg = argv[i];
    if (arg == "-dual") {
      CHECK(!dualize) << "Just one -dual";
      dualize = true;

    } else if (arg == "-no-normalize") {
      CHECK(normalize) << "Just one -no-normalize";
      normalize = false;

    } else if (name.empty()) {
      name = arg;

    } else if (file.empty()) {
      file = arg;

    } else {

      Usage();
      return -1;
    }
  }

  if (name.empty()) {
    Usage();
    return -1;
  }

  if (file.empty()) {
    if (dualize) {
      file = std::format("{}-dual.stl", name);
    } else {
      file = std::format("{}.stl", name);
    }
  }

  if (dualize) {
    name = std::format("{}_dual", name);
  }

  Save(dualize, normalize, name, file);

  return 0;
}
