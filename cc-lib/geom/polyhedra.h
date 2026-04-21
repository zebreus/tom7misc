
// Code for working with polyhedra (especially convex polyhedra)
// and projections of them to 2D. From the ruperts project, but
// "cleaned up."

#ifndef _CC_LIB_GEOM_POLYHEDRA_H
#define _CC_LIB_GEOM_POLYHEDRA_H

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
using vec3 = yocto::vec<double, 3>;
using vec4 = yocto::vec<double, 4>;
using mat4 = yocto::mat<double, 4>;
using quat4 = yocto::quat<double, 4>;
using frame3 = yocto::frame<double, 3>;
using frame2 = yocto::frame<double, 2>;

inline constexpr uint8_t SYM_UNKNOWN = 0b0;
inline constexpr uint8_t SYM_TETRAHEDRAL = 0b1;
inline constexpr uint8_t SYM_OCTAHEDRAL = 0b10;
inline constexpr uint8_t SYM_ICOSAHEDRAL = 0b100;
using SymmetryGroup = uint8_t;

// We never change the connectivity of the objects
// in question, so we can avoid copying the faces.
struct Faces {
  // Each face is represented as the list of vertex (indices) that
  // circumscribe it. The vertices may appear in clockwise or
  // counter-clockwise order.
  std::vector<std::vector<int>> v;
  // For each vertex id, its immediate neighbors, in ascending order.
  std::vector<std::vector<int>> neighbors;
  // An arbitrary triangulation of the polyhedron (each "face" is a
  // triangle, but now faces can be coplanar).
  std::vector<std::tuple<int, int, int>> triangulation;

  // Edges in the polyhedron (not its triangulation). Each one
  // connects two vertices and two faces.
  struct Edge {
    // v0 < v1
    int v0, v1;
    // f0 < f1
    // Indices into the v array.
    int f0, f1;
  };
  std::vector<Edge> edges;

  // Number of vertices we expect.
  int NumVertices() const { return (int)neighbors.size(); }

  // Computes the neighbors and triangulation from the faces. This
  // assumes that every vertex is on at least one face, which will be
  // true for well-formed polyhedra.
  Faces(int num_vertices, std::vector<std::vector<int>> v);

  Faces() {}

  // Like the constructor, but returns nullptr if something goes wrong
  // (like there are vertices that are not on faces).
  static Faces *Create(int num_vertices, std::vector<std::vector<int>> v);

 private:
  bool Init(int num_vertices, std::vector<std::vector<int>> v);
};

struct Polyhedron {
  // A polyhedron is nominally centered around (0,0).
  // This vector contains the positions of the vertices
  // in the polyhedron. The indices of the vertices are
  // significant.
  std::vector<vec3> vertices;
  // Might be shared with transformations of the
  // polyhedron; a global registry, etc.
  std::shared_ptr<const Faces> faces;
  // The optional name of the polyhedron.
  std::string name;
  // The optional symmetry group(s).
  SymmetryGroup symmetry = SYM_UNKNOWN;
};

// A polyhedron projected to 2D. This generally creates
// overlapping polygons, possibly also with coincident
// vertices.
struct Mesh2D {
  // As above.
  std::vector<vec2> vertices;
  // e.g. shared with the Polyhedron.
  std::shared_ptr<const Faces> faces;
};

inline vec4 VecFromQuat(const quat4 &q) {
  return vec4{.x = q.x, .y = q.y, .z = q.z, .w = q.w};
}

inline quat4 QuatFromVec(const vec4 &q) {
  return quat4{.x = q.x, .y = q.y, .z = q.z, .w = q.w};
}

std::string VecString(const vec3 &v);
std::string VecString(const vec2 &v);
std::string Points2DString(const std::vector<vec2> &v);
std::string QuatString(const quat4 &q);
std::string FrameString(const frame3 &f);

inline bool TriangleIsDegenerate(const vec3 &v0,
                                 const vec3 &v1,
                                 const vec3 &v2) {
  vec3 v0v1 = v1 - v0;
  vec3 v0v2 = v2 - v0;
  vec3 cross_product = yocto::cross(v0v1, v0v2);
  // This is a about 2e-16, which is pretty generous.
  return yocto::length_squared(cross_product) <
    std::numeric_limits<double>::epsilon();
}

// Returns a frame representing rotation by angle around the origin.
inline frame2 rotation_frame2(double angle) {
  auto s = std::sin(angle);
  auto c = std::cos(angle);
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
inline double SignedDistanceToEdge(const vec2 &v0, const vec2 &v1,
                                   const vec2 &p) {
  vec2 edge = v1 - v0;
  vec2 p_edge = p - v0;

  double cx = yocto::cross(edge, p_edge);

  double dotprod = yocto::dot(p_edge, edge);
  double sqlen = yocto::dot(edge, edge);

  const bool neg = cx < 0.0;
  if (dotprod <= 0.0) {
    const double len = yocto::length(p_edge);
    return neg ? -len : len;
  } else if (dotprod >= sqlen) {
    const double len = yocto::length(p - v1);
    return len ? -len : len;
  }

  // Signed. Negative means on the left.
  return cx / yocto::length(edge);
}


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
  // the origin. These conditions are not checked.
  PolyTester2D(std::span<const vec2> poly);

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


// Rotate (and translate, if the frame contains a translation) the polyhedron.
// They share the same faces pointer.
inline Polyhedron Rotate(const Polyhedron &p, const frame3 &frame) {
  Polyhedron ret = p;
  for (vec3 &v : ret.vertices) {
    v = yocto::transform_point(frame, v);
  }
  return ret;
}

inline Polyhedron Rotate(const Polyhedron &p, const quat4 &quat) {
  return Rotate(p, yocto::rotation_frame(quat));
}

// Compute the polar dual of the polyhedron. The position matters
// here; you usually want the "center" of the polyhedron at the
// origin.
Polyhedron DualizePoly(const Polyhedron &p);

// Shadow(Rotate(frame, p))
Mesh2D RotateAndProject(const frame3 &frame, const Polyhedron &p);

// Reflect the polyhedron across the XY plane.
// Shares faces with the argument (but they will be inside-out).
Polyhedron ReflectXY(const Polyhedron &p);

inline Mesh2D Translate(const Mesh2D &m, const vec2 &t) {
  Mesh2D ret = m;
  for (vec2 &v : ret.vertices) {
    v += t;
  }
  return ret;
}

bool IsHullConvex(std::span<const vec2> points,
                  std::span<const int> polygon);

bool IsPolyConvex(std::span<const vec2> poly);

// Screen clockwise = cartesian CCW.
bool IsConvexAndScreenClockwise(std::span<const vec2> poly);

// Maximum distance between any two points.
// Note: This is non-standard.
double Diameter(const Polyhedron &p);

// Normalize so that the most distant point from the origin is
// distance 1. This really only makes sense if the polyhedron
// contains the origin (and ideally is centered around it).
Polyhedron NormalizeRadius(const Polyhedron &p);

// Move the polyhedron so that the origin is at the center of
// mass of its vertices.
Polyhedron Recenter(const Polyhedron &p);

// Scale all vertices by the factor.
Polyhedron Scale(const Polyhedron &p, double s);

// Create the shadow of the polyhedron on the x-y plane.
Mesh2D Shadow(const Polyhedron &p);

// Non-negative distance to hull.
double DistanceToHull(
    const std::vector<vec2> &points, const std::vector<int> &hull,
    const vec2 &pt);

double SquaredDistanceToPoly(
    const std::vector<vec2> &poly, const vec2 &pt);

// Return the closest point (could be on an edge or a vertex) on
// the hull, and its distance.
std::pair<vec2, double> ClosestPointOnHull(
    const std::vector<vec2> &points, const std::vector<int> &hull,
    const vec2 &pt);

double DistanceToMesh(const Mesh2D &mesh, const vec2 &pt);


// Faces of a polyhedron must be planar. This computes the
// total error across all faces. If it is far from zero,
// something is wrong, but exact zero is not expected due
// to floating point imprecision.
double PlanarityError(const Polyhedron &p);

// Same idea, but for a single 3D point set. This number
// depends on the order of vertices, but it is zero for
// planar sets.
double PlanarityError(const std::vector<vec3> &pts);

inline vec2 Project(const vec3 &point, const mat4 &proj) {
  vec4 pp = proj * vec4{.x = point.x, .y = point.y, .z = point.z, .w = 1.0};
  return vec2{.x = pp.x / pp.w, .y = pp.y / pp.w};
}

// Point-in-polygon test using the even-odd algorithm.
// Takes a vertex buffer and indices into that set.
bool PointInPolygon(const vec2 &point,
                    const std::vector<vec2> &vertices,
                    const std::vector<int> &polygon);

// Takes the polygon directly as vertices.
bool PointInPolygon(const vec2 &point,
                    const std::vector<vec2> &polygon);

// Is pt strictly within the triangle a-b-c? Exact. Works with both
// winding orders.
bool InTriangle(const vec2 &a, const vec2 &b, const vec2 &c,
                const vec2 &pt);

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

// Generate little tetrahedra at the points, for debugging.
void DebugPointCloudAsSTL(const std::vector<vec3> &vertices,
                          std::string_view filename);

// Save polyhedron as JSON.
void SaveAsJSON(const Polyhedron &poly, std::string_view filename);

// True if the faces are very close to parallel. The face indices
// must be in bounds!
bool FacesParallel(const Polyhedron &poly, int face1, int face2);

// A regular n-gon, extruded to the given depth. Centered at
// the origin. num_points is n, the number of points on the
// top and bottom faces; this will have 2n vertices.
Polyhedron NPrism(int64_t num_points, double depth);

// Like NPrism, but an anti-prism has triangular side faces, and the
// bottom and top faces have their vertices interleaved.
Polyhedron NAntiPrism(int64_t num_points, double depth);

// Takes ownership of the vertices, which should be a convex hull.
// Creates faces as all planes where all the other points are on one
// side. This is not fast; it's intended for a small number of
// vertices. The Faces pointer in the returned Polyhedron is owned
// by the caller. Can fail if the points are not actually a convex
// hull, returning nullopt.
std::optional<Polyhedron> PolyhedronFromConvexVertices(
    std::vector<vec3> vertices, std::string_view name = "");

// First compute the convex hull and discard points outside it.
// Then, the same as above. This only fails for degenerate point sets.
std::optional<Polyhedron> PolyhedronFromVertices(
    std::vector<vec3> vertices, std::string_view name = "");

// Generate some polyhedra. Many of these are not fast because they do
// some kind of search (e.g. a polynomial-time convex hull
// calculation) to find the connectivity. You should reuse the base
// shape rather than than repeatedly generating new ones.

// Platonic
Polyhedron Tetrahedron();
Polyhedron Cube();
Polyhedron Dodecahedron();
Polyhedron Icosahedron();
Polyhedron Octahedron();

// Archimedean
Polyhedron TruncatedTetrahedron();
Polyhedron Cuboctahedron();
Polyhedron TruncatedCube();
Polyhedron TruncatedOctahedron();
Polyhedron Rhombicuboctahedron();
Polyhedron TruncatedCuboctahedron();
Polyhedron SnubCube();
Polyhedron Icosidodecahedron();
Polyhedron TruncatedDodecahedron();
Polyhedron TruncatedIcosahedron();
Polyhedron Rhombicosidodecahedron();
Polyhedron TruncatedIcosidodecahedron();
Polyhedron SnubDodecahedron();

// Catalan
Polyhedron TriakisTetrahedron();
Polyhedron RhombicDodecahedron();
Polyhedron TriakisOctahedron();
Polyhedron TetrakisHexahedron();
Polyhedron DeltoidalIcositetrahedron();
Polyhedron DisdyakisDodecahedron();
Polyhedron DeltoidalHexecontahedron();
Polyhedron PentagonalIcositetrahedron();
Polyhedron RhombicTriacontahedron();
Polyhedron TriakisIcosahedron();
Polyhedron PentakisDodecahedron();
Polyhedron DisdyakisTriacontahedron();
Polyhedron PentagonalHexecontahedron();

// Other special
Polyhedron Noperthedron();
Polyhedron Onperthedron();

#endif
