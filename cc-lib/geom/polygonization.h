
#ifndef _CC_LIB_GEOM_POLYGONIZATION_H
#define _CC_LIB_GEOM_POLYGONIZATION_H

#include <string_view>
#include <tuple>
#include <variant>
#include <vector>

#include "yocto-math.h"

// Creates a high-quality polygonization (using convex polygons up
// to some limit in vertices) of a shape.
struct Polygonization {
  using vec2 = yocto::vec<double, 2>;

  using Path = std::vector<vec2>;

  // The paths may not be self-intersecting. Since the paths must be
  // closed, we use the even/odd rule to define the shape. Winding
  // order can therefore be CW or CCW, or even a mix within the same
  // shape.
  struct Shape {
    std::vector<Path> points;
  };

  struct Mesh {
    // This typically includes the exact vertices from the input
    // (unless they are unnecessary, e.g. exactly colinear) and may
    // also include new vertices ("Steiner points") to create a
    // high-quality polygonization.
    std::vector<vec2> vertices;
    // All polygons are convex. No overlap. Cartesian CW (screen CCW)
    // winding order.
    std::vector<std::vector<int>> polygons;
  };

  using PolygonizeResult = std::variant<
    // Error message, e.g. on a bad input shape.
    std::string_view,
    // Successful mesh
    Mesh>;

  // Polygonize the shape into convex polygons with no more than
  // max_vertices each.
  static PolygonizeResult Polygonize(const Shape &shape,
                                     int max_vertices);

  // An explicitly triangular mesh.
  struct TriangularMesh {
    std::vector<vec2> vertices;
    std::vector<std::tuple<int, int, int>> triangles;
  };

  using TriangulateResult = std::variant<std::string_view,
                                         TriangularMesh>;
  static TriangulateResult Triangulate(const Shape &shape);
};

#endif
