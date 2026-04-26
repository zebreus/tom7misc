
#ifndef _ALBRECHT_POLY_UTIL_H
#define _ALBRECHT_POLY_UTIL_H

#include <string_view>

#include "geom/polyhedra.h"
#include "geom/mesh.h"

TriangularMesh3D PolyToTriangularMesh(const Polyhedron &poly);

void SaveAsSTL(const Polyhedron &poly, std::string_view filename);

#endif
