// Basic routines for polygons (2D).
// For convex hull, see hull-2d.h.
// For triangulation and polygonization, see polygonization.h.

#ifndef _CC_LIB_GEOM_POLYGONS_H
#define _CC_LIB_GEOM_POLYGONS_H

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "base/logging.h"
#include "yocto-math.h"

using vec2 = yocto::vec<double, 2>;
using frame2 = yocto::frame<double, 2>;

// For interoperability, we avoid wrapping the points. Most methods
// place some preconditions on the types of polygons that are allowed
// (e.g. that they have at least three points, are simple, use a
// specific, winding order etc.)
using Polygon = std::vector<vec2>;
using PolygonConstView = std::span<const vec2>;

bool IsPolyConvex(PolygonConstView poly);

// Screen clockwise = cartesian CCW.
bool IsConvexAndScreenClockwise(PolygonConstView poly);

double SquaredDistanceToPoly(PolygonConstView poly, const vec2 &pt);

// Point-in-polygon test using the even-odd algorithm.
// Takes a vertex buffer and indices into that set.
bool PointInPolygon(const std::vector<vec2> &vertices,
                    const std::vector<int> &polygon,
                    const vec2 &point);

// Takes the polygon directly as vertices.
bool PointInPolygon(PolygonConstView polygon, const vec2 &point);

// Is pt strictly within the triangle a-b-c? Exact. Works with both
// winding orders.
bool InTriangle(const vec2 &a, const vec2 &b, const vec2 &c,
                const vec2 &pt);


// Returns a frame representing rotation by angle around the origin.
inline frame2 rotation_frame2(double angle) {
  double s = std::sin(angle);
  double c = std::cos(angle);
  return {{c, s}, {-s, c}, {0.0, 0.0}};
}

// Euclidean distance (non-negative) to the line segment from
// the point. This may be one of the endpoints.
double PointLineDistance(
    // Line segment
    const vec2 &v0, const vec2 &v1,
    // Point to test
    const vec2 &pt);
// Same, but squared.
double SquaredPointLineDistance(
    // Line segment
    const vec2 &v0, const vec2 &v1,
    // Point to test
    const vec2 &pt);

// For an oriented edge from v0 to v1, return the signed
// distance to that edge. Negative distance means to the left.
// Note: This cannot be used to find the signed distance to
// a polygon, because of ambiguity when the closest point is
// one of the vertices.
double SignedDistanceToEdge(const vec2 &v0, const vec2 &v1,
                            const vec2 &p);

// Signed distance to the triangle from the point p. Vertex order
// does not matter. Negative sign means the interior of the triangle.
double TriangleSignedDistance(vec2 p0, vec2 p1, vec2 p2, vec2 p);

// Positive if screen clockwise (cartesian ccw) winding order;
// negative for screen ccw (cartesian cw).
double SignedAreaOfConvexPoly(std::span<const vec2> points);

// Precomputation for testing points in a polygon. This
// should be faster if you need to call PointInPolygon
// many times for the same polygon.
#define POLYTESTER_USE_BB 0
struct PolyTester2D {
  static constexpr bool SELF_CHECK = false;

  // The polygon must be convex, screen clockwise, and must include
  // the origin. These conditions are not checked. The polygon must
  // outlive this instance.
  PolyTester2D(PolygonConstView poly);

  // Returns nullopt if the point is inside. Otherwise, minimum squared
  // distance to the polygon.
  std::optional<double> SquaredDistanceOutside(const vec2 &pt) const;

  bool IsInside(const vec2 &pt) const {
    return !SquaredDistanceOutside(pt).has_value();
  }

 private:
  double SquaredDistanceToPoly(const vec2 &pt) const;
  bool PointInPolygon(const vec2 &point) const;

  std::span<const vec2> poly;
  // parallel to the vertices. Represents the edge from the vertex
  // to the next one.
  std::vector<vec2> edges;
  std::vector<double> edge_sqlens;

  #if POLYTESTER_USE_BB
  // Bounding box.
  double min_x = std::numeric_limits<double>::infinity();
  double max_x = -std::numeric_limits<double>::infinity();
  double min_y = std::numeric_limits<double>::infinity();
  double max_y = -std::numeric_limits<double>::infinity();
  #endif
};


#endif
