
#ifndef _RUPERTS_POLYHEDRA_H
#define _RUPERTS_POLYHEDRA_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>

#include "arcfour.h"
#include "base/logging.h"
#include "randutil.h"

#include "yocto_matht.h"

using vec2 = yocto::vec<double, 2>;
using vec3 = yocto::vec<double, 3>;
using vec4 = yocto::vec<double, 4>;
using mat4 = yocto::mat<double, 4>;
using quat4 = yocto::quat<double, 4>;
using frame3 = yocto::frame<double, 3>;

// We never change the connectivity of the objects
// in question, so we can avoid copying the faces.
//
// A face is represented as the list of vertex (indices)
// that circumscribe it. The vertices may appear in
// clockwise or counter-clockwise order.
struct Faces {
  std::vector<std::vector<int>> v;
  // Number of vertices we expect.
  int num_vertices = 0;
};

struct Polyhedron {
  // A polyhedron is nominally centered around (0,0).
  // This vector contains the positions of the vertices
  // in the polyhedron. The indices of the vertices are
  // significant.
  std::vector<vec3> vertices;
  // Not owned.
  const Faces *faces = nullptr;
};

// A polyhedron projected to 2D. This generally creates
// overlapping polygons, possibly also with coincident
// vertices.
struct Mesh2D {
  // As above.
  std::vector<vec2> vertices;
  // Not owned.
  const Faces *faces = nullptr;
};

inline vec4 VecFromQuat(const quat4 &q) {
  return vec4{.x = q.x, .y = q.y, .z = q.z, .w = q.w};
}

inline quat4 QuatFromVec(const vec4 &q) {
  return quat4{.x = q.x, .y = q.y, .z = q.z, .w = q.w};
}

std::string VecString(const vec3 &v);
std::string FrameString(const frame3 &f);
std::string FormatNum(uint64_t n);

// For an oriented edge from v0 to v1, return the signed
// distance to that edge. Negative distance means to the left.
// Note: This cannot be used to find the signed distance to
// a polygon, because of ambiguity when the closest point is
// one of the vertices.
inline double SignedDistanceToEdge(const vec2 &v0, const vec2 &v1,
                                   const vec2 &p) {
  vec2 edge = v1 - v0;

  vec2 p_edge = p - v0;
  double cx = yocto::cross(edge, p_edge);
  // Signed. Negative means on the left.
  double dist = cx / yocto::length(edge);

  // Consider the endpoints. These are unsigned distances.
  double d0 = yocto::length(p_edge);
  double d1 = yocto::length(p - v1);

  if (d0 < std::abs(dist)) dist = d0 * yocto::sign(cx);
  if (d1 < std::abs(dist)) dist = d1 * yocto::sign(cx);

  return dist;
}

// Unsigned distance to the edge.
inline double DistanceToEdge(const vec2 &v0, const vec2 &v1,
                             const vec2 &p) {
  vec2 edge = v1 - v0;
  vec2 p_edge = p - v0;
  double cx = yocto::cross(edge, p_edge);
  double dist = std::abs(cx / yocto::length(edge));
  double d0 = yocto::length(p_edge);
  double d1 = yocto::length(p - v1);

  return std::min(std::min(d0, d1), dist);
}

// Rotate the polyhedron. They share the same faces pointer.
inline Polyhedron Rotate(const Polyhedron &p, const frame3 &frame) {
  Polyhedron ret = p;
  for (vec3 &v : ret.vertices) {
    v = yocto::transform_point(frame, v);
  }
  return ret;
}

// Create the shadow of the polyhedron on the x-y plane.
// The mesh's faces object aliases the input polyhedron's.
Mesh2D Shadow(const Polyhedron &p);

double DistanceToHull(
    const Mesh2D &mesh, const std::vector<int> &hull,
    const vec2 &pt);

// Returns the convex hull (a single polygon) as indices into the
// vertex list (e.g. Mesh2D::vertices). This is the intuitive "gift
// wrapping" algorithm; much faster approaches exist, but we have a
// small number of vertices and this is in the outer loop.
std::vector<int> ConvexHull(const std::vector<vec2> &vertices);

// Faces of a polyhedron must be planar. This computes the
// total error across all faces. If it is far from zero,
// something is wrong, but exact zero is not expected due
// to floating point imprecision.
double PlanarityError(const Polyhedron &p);

// Generate some polyhedra. Note that each call new-ly allocates a
// Faces object, which is then owned by the caller. Some of these are
// not fast because they do some kind of search (e.g. a
// polynomial-time convex hull calculation) to find the connectivity.
// It's better to reuse the base shape and share the faces object
// than it is to keep generating new ones.
Polyhedron Cube();
Polyhedron Dodecahedron();
Polyhedron SnubCube();

inline quat4 RandomQuaternion(ArcFour *rc) {
  const auto &[x, y, z, w] = RandomUnit4D(rc);
  return quat4{.x = x, .y = y, .z = z, .w = w};
}

inline vec2 Project(const vec3 &point, const mat4 &proj) {
  vec4 pp = proj * vec4{.x = point.x, .y = point.y, .z = point.z, .w = 1.0};
  return vec2{.x = pp.x / pp.w, .y = pp.y / pp.w};
}

// Point-in-polygon test using the winding number algorithm
bool PointInPolygon(const vec2 &point,
                    const std::vector<vec2> &vertices,
                    const std::vector<int> &polygon);

inline bool InMesh(const Mesh2D &mesh, const vec2 &pt) {
  for (const std::vector<int> &face : mesh.faces->v)
    if (PointInPolygon(pt, mesh.vertices, face))
      return true;

  return false;
}

inline bool InHull(const Mesh2D &mesh, const std::vector<int> &hull,
                   const vec2 &pt) {
  return PointInPolygon(pt, mesh.vertices, hull);
}

#endif
