
#include "polygons.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <span>
#include <vector>

#include "yocto-math.h"

template<typename T>
inline int sgn(T val) {
  return (T(0) < val) - (val < T(0));
}

double TriangleSignedDistance(vec2 p0, vec2 p1, vec2 p2, vec2 p) {
  // This function only:
  // Derived from code by Inigo Quilez; MIT license. See LICENSES.

  vec2 e0 = p1 - p0;
  vec2 e1 = p2 - p1;
  vec2 e2 = p0 - p2;

  vec2 v0 = p - p0;
  vec2 v1 = p - p1;
  vec2 v2 = p - p2;

  vec2 pq0 = v0 - e0 * std::clamp(dot(v0, e0) / dot(e0, e0), 0.0, 1.0);
  vec2 pq1 = v1 - e1 * std::clamp(dot(v1, e1) / dot(e1, e1), 0.0, 1.0);
  vec2 pq2 = v2 - e2 * std::clamp(dot(v2, e2) / dot(e2, e2), 0.0, 1.0);

  double s = e0.x * e2.y - e0.y * e2.x;

  vec2 d1 = vec2(dot(pq0, pq0), s * (v0.x * e0.y - v0.y * e0.x));
  vec2 d2 = vec2(dot(pq1, pq1), s * (v1.x * e1.y - v1.y * e1.x));
  vec2 d3 = vec2(dot(pq2, pq2), s * (v2.x * e2.y - v2.y * e2.x));

  vec2 d = min(min(d1, d2), d3);

  return -std::sqrt(d.x) * sgn(d.y);
}

bool InTriangle(const vec2 &a, const vec2 &b, const vec2 &c,
                const vec2 &pt) {
  // The idea behind this test is that for each edge, we check
  // to see if the test point is on the same side as a reference
  // point, which is the third point of the triangle.
  auto SameSide = [](const vec2 &u, const vec2 &v,
                     const vec2 &p1, const vec2 &p2) {
      vec2 edge = v - u;
      double c1 = cross(edge, p1 - u);
      double c2 = cross(edge, p2 - u);

      int s1 = sgn(c1);
      int s2 = sgn(c2);

      // Note that this excludes the edge itself.
      return s1 != 0 && s2 != 0 && s1 == s2;
    };

  return SameSide(a, b, c, pt) &&
    SameSide(b, c, a, pt) &&
    SameSide(c, a, b, pt);
}

// Return the minimum distance between the point and the line segment.
double SquaredPointLineDistance(
    // Line segment
    const vec2 &v0, const vec2 &v1,
    // Point to test
    const vec2 &pt) {

  #if 0
  const vec2 c = ClosestPointOnSegment(v0, v1, pt);
  const double dx = pt.x - c.x;
  const double dy = pt.y - c.y;
  return dx * dx + dy * dy;
  #else
  // This approach is about 10% faster than the above.

  const vec2 edge = v1 - v0;

  const double sqlen = length_squared(edge);

  // For a degnerate segment, there's just one distance to consider.
  if (sqlen == 0.0) {
    return distance_squared(pt, v0);
  }

  const vec2 c = pt - v0;

  // Project p onto the vector.
  const double dotprod = dot(c, edge);

  if (dotprod <= 0.0) {
    // Before the starting point.
    return distance_squared(pt, v0);
  } else if (dotprod >= sqlen) {
    // After the ending point.
    return distance_squared(pt, v1);
  } else {
    const double tf = dotprod / sqlen;

    // Between the two points. The closest point on the segment
    // will be on a line perpendicular to the segment. So we
    // can actually use the Pythagorean theorem to get a^2 here:
    //
    //         pt    c
    //         | `-.
    //      a  |    `-.        a^2 + b^2 = c^2
    //  v1-----x-------v0      so
    //             b           a^2 = c^2 - b^2

    const double bsquared = tf * dotprod;
    return length_squared(c) - bsquared;
  }
  #endif
}

double PointLineDistance(
    // Line segment
    const vec2 &v0, const vec2 &v1,
    // Point to test
    const vec2 &pt) {
  return std::sqrt(SquaredPointLineDistance(v0, v1, pt));
}

bool IsPolyConvex(std::span<const vec2> poly) {
  if (poly.size() < 3) return false;
  std::optional<int> s;
  for (int i = 0; i < (int)poly.size(); i++) {
    const vec2 &p0 = poly[i];
    const vec2 &p1 = poly[(i + 1) % poly.size()];
    const vec2 &p2 = poly[(i + 2) % poly.size()];

    vec2 e1 = p1 - p0;
    vec2 e2 = p2 - p1;

    double cx = cross(e1, e2);
    if (std::abs(cx) < 1e-10) continue;
    int sign = sgn(cx);
    if (s.has_value() && s.value() != sign)
      return false;
    s = {sign};
  }
  return true;
}

bool IsConvexAndScreenClockwise(std::span<const vec2> poly) {
  if (poly.size() < 3) return false;

  for (int i = 0; i < (int)poly.size(); i++) {
    const vec2 &va = poly[i];
    const vec2 &vb = poly[(i + 1) % poly.size()];
    const vec2 &vc = poly[(i + 2) % poly.size()];

    double cx = cross(vb - va, vc - vb);
    if (cx < -1.0e-10) {
      return false;
    }
  }

  return true;
}

double SignedAreaOfConvexPoly(std::span<const vec2> pts) {
  if (pts.size() < 3) return 0.0;
  double area = 0.0;
  // Iterate through the polygon vertices, using the shoelace formula.
  for (size_t i = 0; i < pts.size(); i++) {
    const vec2 &v0 = pts[i];
    const vec2 &v1 = pts[(i + 1) % pts.size()];
    area += v0.x * v1.y - v1.x * v0.y;
  }

  return area * 0.5;
}

template<class GetPt>
inline static bool PointInPolygonT(const vec2 &point,
                                   int size,
                                   const GetPt &get_pt) {
  int winding_number = 0;
  for (int i = 0; i < size; i++) {
    const vec2 p0 = get_pt(i);
    const vec2 p1 = get_pt((i + 1) % size);

    // Check if the ray from the point to infinity intersects the edge
    if (point.y > std::min(p0.y, p1.y)) {
      if (point.y <= std::max(p0.y, p1.y)) {
        if (point.x <= std::max(p0.x, p1.x)) {
          if (p0.y != p1.y) {
            double vt = (point.y - p0.y) / (p1.y - p0.y);
            if (point.x < p0.x + vt * (p1.x - p0.x)) {
              winding_number++;
            }
          }
        }
      }
    }
  }

  // Point is inside if the winding number is odd
  return !!(winding_number & 1);
}

bool PointInPolygon(const std::vector<vec2> &vertices,
                    const std::vector<int> &polygon,
                    const vec2 &point) {
  return PointInPolygonT(point, polygon.size(),
                         [&](int idx) {
                           return vertices[polygon[idx]];
                         });
}

bool PointInPolygon(PolygonConstView polygon, const vec2 &point) {
  return PointInPolygonT(point, polygon.size(),
                         [&](int idx) {
                           return polygon[idx];
                         });
}

double SquaredDistanceToPoly(PolygonConstView poly,
                             const vec2 &pt) {
  double best_sqdist = std::numeric_limits<double>::infinity();
  for (int i = 0; i < (int)poly.size(); i++) {
    const vec2 &v0 = poly[i];
    const vec2 &v1 = poly[(i + 1) % poly.size()];

    double sqdist = SquaredPointLineDistance(v0, v1, pt);
    best_sqdist = std::min(best_sqdist, sqdist);
  }
  CHECK(std::isfinite(best_sqdist));
  return best_sqdist;
}

PolyTester2D::PolyTester2D(std::span<const vec2> poly) : poly(poly) {
  if (SELF_CHECK) {
    CHECK(SignedAreaOfConvexPoly(poly) > 0.0);
    CHECK(IsConvexAndScreenClockwise(poly));
  }

  // TODO: Precompute.
  edges.reserve(poly.size());
  edge_sqlens.reserve(poly.size());

  for (int i = 0; i < (int)poly.size(); i++) {
    const vec2 &v0 = poly[i];
    const vec2 &v1 = poly[(i + 1) % poly.size()];
    const vec2 edge = v1 - v0;
    const double sqlen = length_squared(edge);
    edges.push_back(edge);
    edge_sqlens.push_back(sqlen);
    #if POLYTESTER_USE_BB
    min_x = std::min(min_x, v0.x);
    max_x = std::max(max_x, v0.x);
    min_y = std::min(min_y, v0.y);
    max_y = std::max(max_y, v0.y);
    #endif
  }
}

bool PolyTester2D::PointInPolygon(const vec2 &point) const {
  #if POLYTESTER_USE_BB
  if (point.x < min_x) return false;
  if (point.x > max_x) return false;

  if (point.y < min_y) return false;
  if (point.y > max_y) return false;
  #endif

  for (int i = 0; i < (int)poly.size(); i++) {
    const vec2 &v0 = poly[i];
    const vec2 &edge = edges[i];
    const vec2 pt_vec = point - v0;

    // Cross product: edge.x * pt_vec.y - edge.y * pt_vec.x
    // If negative, then the point is on the wrong side of the edge.
    if (edge.x * pt_vec.y < edge.y * pt_vec.x) {
      return false;
    }
  }

  // If the point was not clearly outside any edge, it's inside or on
  // the boundary.
  return true;
}


double PolyTester2D::SquaredDistanceToPoly(const vec2 &pt) const {
  double best_sqdist = std::numeric_limits<double>::infinity();
  for (int i = 0; i < (int)poly.size(); i++) {
    const vec2 &v0 = poly[i];

    // This is SquaredPointLineDistance, but we use some
    // precomputed facts.

    const vec2 &edge = edges[i];
    const double sqlen = edge_sqlens[i];

    // For a degnerate segment, there's just one distance to consider.
    if (sqlen == 0.0) {
      best_sqdist = std::min(best_sqdist, distance_squared(pt, v0));
      continue;
    }

    const vec2 c = pt - v0;

    // Project p onto the vector.
    const double dotprod = dot(c, edge);

    if (dotprod <= 0.0) {
      // Before the starting point.
      best_sqdist = std::min(best_sqdist, distance_squared(pt, v0));
    } else if (dotprod >= sqlen) {
      // After the ending point.
      const vec2 &v1 = poly[(i + 1) % poly.size()];
      best_sqdist = std::min(best_sqdist, distance_squared(pt, v1));
    } else {
      const double tf = dotprod / sqlen;

      const double bsquared = tf * dotprod;
      const double sqdist = length_squared(c) - bsquared;
      best_sqdist = std::min(best_sqdist, sqdist);
    }
  }

  CHECK(std::isfinite(best_sqdist));
  return best_sqdist;
}



std::optional<double>
PolyTester2D::SquaredDistanceOutside(const vec2 &pt) const {
  if (PointInPolygon(pt)) {
    return std::nullopt;
  }

  return SquaredDistanceToPoly(pt);
}
