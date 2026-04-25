
#include "symmetry-groups.h"

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "geom/polyhedra.h"
#include "yocto-math.h"

static void Size() {
  SymmetryGroups groups;

  Print(
      AYELLOW("tetrahedron") " has " ACYAN("{}") " rotations, {} pts\n",
      groups.tetrahedron.rots.size(), groups.tetrahedron.points);
  Print(
      AYELLOW("octahedron") " has " ACYAN("{}") " rotations, {} pts\n",
      groups.octahedron.rots.size(), groups.octahedron.points);
  Print(
      AYELLOW("icosahedron") " has " ACYAN("{}") " rotations, {} pts\n",
      groups.icosahedron.rots.size(), groups.icosahedron.points);
}

static void IcosahedronSymmetry() {
  SymmetryGroups groups;
  Polyhedron ico = Icosahedron();

  for (const auto& rot : groups.icosahedron.rots) {
    for (const auto& v : ico.vertices) {
      auto p = yocto::transform_point(rot, v);

      bool found = false;
      for (const auto& ov : ico.vertices) {
        if (yocto::length(p - ov) < 1e-4) {
          found = true;
          break;
        }
      }
      CHECK(found) << "After rotating, the point should be on the "
        "icosahedron!";
    }
  }
}

static void TetrahedronSymmetry() {
  SymmetryGroups groups;
  Polyhedron tet = Tetrahedron();

  for (const auto& rot : groups.tetrahedron.rots) {
    for (const auto& v : tet.vertices) {
      auto p = yocto::transform_point(rot, v);

      bool found = false;
      for (const auto& ov : tet.vertices) {
        if (yocto::length(p - ov) < 1e-4) {
          found = true;
          break;
        }
      }
      CHECK(found) << "After rotating, the point should be on the "
        "tetrahedron!";
    }
  }
}

static void OctahedronSymmetry() {
  SymmetryGroups groups;
  Polyhedron oct = Octahedron();

  for (const auto& rot : groups.octahedron.rots) {
    for (const auto& v : oct.vertices) {
      auto p = yocto::transform_point(rot, v);

      bool found = false;
      for (const auto& ov : oct.vertices) {
        if (yocto::length(p - ov) < 1e-4) {
          found = true;
          break;
        }
      }
      CHECK(found) << "After rotating, the point should be on the "
        "octahedron!";
    }
  }
}



int main(int argc, char **argv) {
  ANSI::Init();

  Size();
  IcosahedronSymmetry();
  TetrahedronSymmetry();
  OctahedronSymmetry();

  Print("OK\n");
  return 0;
}
