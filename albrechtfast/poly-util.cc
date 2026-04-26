
#include "poly-util.h"

#include <string_view>

#include "geom/mesh.h"
#include "geom/polyhedra.h"

TriangularMesh3D PolyToTriangularMesh(const Polyhedron &poly) {
  return TriangularMesh3D{.vertices = poly.vertices,
    .triangles = poly.faces->triangulation};
}

void SaveAsSTL(const Polyhedron &poly, std::string_view filename) {
  TriangularMesh3D mesh = PolyToTriangularMesh(poly);
  OrientMesh(&mesh);
  return SaveAsSTL(mesh, filename, poly.name);
}
