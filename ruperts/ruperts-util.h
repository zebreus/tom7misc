
#ifndef _RUPERTS_RUPERTS_UTIL_H
#define _RUPERTS_RUPERTS_UTIL_H

#include <algorithm>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "arcfour.h"
#include "geom/mesh.h"
#include "geom/polyhedra.h"
#include "randutil.h"
#include "yocto-math.h"

std::string FormatNum(uint64_t n);

// Unpack a rigid frame into a rotation (as a normalized quaternion)
// and a translation vector. If the matrix is not actually a rigid
// transform, then the result may not be meaningful.
std::pair<quat4, vec3> UnpackFrame(const frame3 &f);

// For normalized vectors a and b (interpreted as orientations on
// the sphere), compute a rotation from a to b.
quat4 RotationFromAToB(const vec3 &a, const vec3 &b);

// See big-polyhedra.
vec3 ViewPosFromNonUnitQuat(const quat4 &q);

inline quat4 RandomQuaternion(ArcFour *rc) {
  const auto &[x, y, z, w] = RandomUnit4D(rc);
  return quat4{.x = x, .y = y, .z = z, .w = w};
}

// Various tests for exactly zero, usually for assertions.

inline bool AllZero(const std::vector<vec2> &vec) {
  for (const vec2 &v : vec)
    if (v.x != 0.0 || v.y != 0.0)
      return false;
  return true;
}

inline bool AllZero(const vec3 &vec) {
  return vec.x == 0.0 && vec.y == 0.0 && vec.z == 0.0;
}

inline bool AllZero(const quat4 &q) {
  return q.x == 0.0 && q.y == 0.0 && q.z == 0.0 && q.w == 0.0;
}

inline bool AllZero(const frame3 &f) {
  return AllZero(f.x) && AllZero(f.y) && AllZero(f.z) && AllZero(f.o);
}

// In the inner loop, we compute a convex hull for a covex polyhedron
// centered at the origin, and then test whether many points are
// inside that hull. If the point's not close to the hull, this can
// be done much faster by just testing whether it's a known distance
// from the hull. This represents the circumscribed circle of the hull,
// centered at the origin.
//
// Note that this requires for correctness (but does not check) that
// the hull contains the origin.
struct HullCircumscribedCircle {
  HullCircumscribedCircle(const std::vector<vec2> &vertices,
                          const std::vector<int> &hull) {
    CHECK(hull.size() != 0);
    max_sqdist = yocto::length_squared(vertices[hull[0]]);
    for (int idx = 1; idx < hull.size(); idx++) {
      const vec2 &v = vertices[hull[idx]];
      max_sqdist = std::max(max_sqdist, yocto::length_squared(v));
    }
  }

  bool DefinitelyOutside(const vec2 &pt) const {
    return yocto::length_squared(pt) > max_sqdist;
  }

  double max_sqdist = 0.0;
};

// Same idea, but with an inscribed circle. This touches edges, not
// vertices.
//
// Note that this requires for correctness (but does not check) that
// the hull contains the origin.
struct HullInscribedCircle {
  HullInscribedCircle(const std::vector<vec2> &vertices,
                      const std::vector<int> &hull) {
    CHECK(hull.size() >= 3) << Points2DString(vertices);

    for (int i = 0; i < hull.size(); ++i) {
      const vec2 &v0 = vertices[hull[i]];
      const vec2 &v1 = vertices[hull[(i + 1) % hull.size()]];

      // Express as v0 + t * edge.
      vec2 edge = v1 - v0;
      // The vector to the closest point will be orthogonal to the edge.
      double t = -yocto::dot(v0, edge) / yocto::length_squared(edge);
      t = std::clamp(t, 0.0, 1.0);
      vec2 closest_point = v0 + t * edge;

      min_sqdist = std::min(min_sqdist, yocto::length_squared(closest_point));
    }
  }

  bool DefinitelyInside(const vec2 &pt) const {
    return yocto::length_squared(pt) < min_sqdist;
  }

  double min_sqdist = std::numeric_limits<double>::max();
};


// Return the minimum distance between the two hulls; assumes that
// the inner is completely inside the outer.
double HullClearance(const std::vector<vec2> &outer_points,
                     const std::vector<int> &outer_hull,
                     const std::vector<vec2> &inner_points,
                     const std::vector<int> &inner_hull);

// The area of the convex hull; should also work for any simple
// polygon.
double AreaOfHull(const PolyhedronMesh2D &mesh,
                  const std::vector<int> &hull);
double AreaOfHull(const std::vector<vec2> &vertices,
                  const std::vector<int> &hull);
// Positive if screen clockwise (cartesian ccw) winding order;
// negative for screen ccw (cartesian cw).
double SignedAreaOfHull(const PolyhedronMesh2D &mesh,
                        const std::vector<int> &hull);

// Standard loss function for solution procedure. This recomputes the
// entire problem; if some of the parameters are fixed (e.g. the outer
// transformation) then you should not use this!
//
// Requires that outer the polyhedron contain the origin after applying
// the frame. This is typical (P/A/C solids all contain it; usually we
// do not translate the outer poly). If you aren't sure, use LossFunction.
double LossFunctionContainsOrigin(const Polyhedron &poly,
                                  const frame3 &outer_frame,
                                  const frame3 &inner_frame);

// As previous, but with two potentially different polyhedra.
double HeteroLossFunctionContainsOrigin(const Polyhedron &outer_poly,
                                        const Polyhedron &inner_poly,
                                        const frame3 &outer_frame,
                                        const frame3 &inner_frame);

// Same, but does not assume the outer polyhedron contains the origin.
double LossFunction(const Polyhedron &poly,
                    const frame3 &outer_frame,
                    const frame3 &inner_frame);

// Same, but also compute a gradient when we have a solution (which
// is slow).
double FullLossContainsOrigin(const Polyhedron &poly,
                              const frame3 &outer_frame,
                              const frame3 &inner_frame);

// Get the ratio inner_area / outer_area, which is a reasonable metric
// for how good the solution is. Will be in [0, 1). Returns nullopt
// if the solution is not valid.
std::optional<double> GetRatio(const Polyhedron &poly,
                               const frame3 &outer_frame,
                               const frame3 &inner_frame);

// Get the minimum distance between the two hulls. Here a larger
// distance is better.
std::optional<double> GetClearance(const Polyhedron &poly,
                                   const frame3 &outer_frame,
                                   const frame3 &inner_frame);

// Sample two faces from the polyhedron that are not parallel to each
// other.
std::pair<int, int> TwoNonParallelFaces(ArcFour *rc, const Polyhedron &poly);


TriangularMesh3D PolyToTriangularMesh(const Polyhedron &poly);

// Creates an approximate sphere by "triforce" subdivision of the
// icosahedron with the given depth (exponential).
TriangularMesh3D ApproximateSphere(int depth);

// Orient the mesh and save as STL.
void SaveAsSTL(const Polyhedron &poly, std::string_view filename);

// Save solution as JSON.
void SaveSolutionAsJSON(const frame3 &outer_frame,
                        const frame3 &inner_frame,
                        std::string_view filename);

// Nickname for abbreviated display. Not canonical; may change.
std::string PolyhedronShortName(std::string_view name);
// Get the proper name from the nickname.
std::string PolyhedronIdFromNickname(std::string_view nickname);
// Get the human name (e.g. with spaces between words).
std::string PolyhedronHumanName(std::string_view name);

// For P/A/C solids. Returns the name of the dual.
std::string DualPolyhedron(std::string_view name);


#endif
