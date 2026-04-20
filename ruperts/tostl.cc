
#include "ansi.h"

#include <cstdlib>
#include <format>
#include <string>
#include <string_view>

#include "base/print.h"
#include "polyhedra.h"
#include "ruperts-util.h"
#include "solutions.h"
#include "yocto-math.h"

using frame3 = yocto::frame<double, 3>;
using Nopert = SolutionDB::Nopert;


static void Save(bool dualize, std::string_view name, std::string_view file) {
  SolutionDB db;
  Polyhedron p = db.AnyPolyhedronByName(name);
  if (dualize) p = DualizePoly(p);
  Polyhedron polyhedron = NormalizeRadius(p);

  // This orients the mesh:
  SaveAsSTL(polyhedron, file);
}

static void Usage() {
  Print("./tostl.exe [-dual] polyhedronname [output.stl]\n");
}

int main(int argc, char **argv) {
  ANSI::Init();

  std::string name;
  std::string file;

  bool dualize = false;
  for (int i = 1; i < argc; i++) {
    std::string_view arg = argv[i];
    if (arg == "-dual") {
      CHECK(!dualize) << "Just one -dual";
      dualize = true;

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
      file = std::format("{}.stl", name);
    } else {
      file = std::format("{}-dual.stl", name);
    }
  }

  Save(dualize, name, file);

  return 0;
}
