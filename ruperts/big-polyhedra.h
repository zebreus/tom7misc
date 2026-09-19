
#ifndef _RUPERTS_BIG_POLYHEDRA_H
#define _RUPERTS_BIG_POLYHEDRA_H

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "base/logging.h"
#include "bignum/big-overloads.h"
#include "bignum/big-vec.h"
#include "bignum/big.h"
#include "geom/polyhedra.h"
#include "hashing.h"

// Quaternion as xi + yj + zk + w
struct BigQuat {
  BigQuat(BigRat x, BigRat y, BigRat z, BigRat w) :
    x(std::move(x)), y(std::move(y)), z(std::move(z)), w(std::move(w)) {}
  BigQuat() {}
  BigRat x = BigRat(0), y = BigRat(0), z = BigRat(0), w = BigRat(1);
};

BigQuat MakeBigQuat(const quat4 &smallquat);
BigQuat ApproxBigQuat(const quat4 &smallquat, int64_t max_denom);

struct BigPoly {
  std::vector<BigVecQ3> vertices;
  std::shared_ptr<const Faces> faces;
  std::string name;
};

// (big version of PolyhedronMesh2D)
struct BigMesh2D {
  std::vector<BigVecQ2> vertices;
  std::shared_ptr<const Faces> faces;
};

// Rigid frames stored as a column-major affine transform matrix.
struct BigFrame {
  BigVecQ3 x = {BigRat(1), BigRat(0), BigRat(0)};
  BigVecQ3 y = {BigRat(0), BigRat(1), BigRat(0)};
  BigVecQ3 z = {BigRat(0), BigRat(0), BigRat(1)};
  BigVecQ3 o = {BigRat(0), BigRat(0), BigRat(0)};

  BigVecQ3& operator[](int i) {
    switch (i) {
    case 0: return x;
    case 1: return y;
    case 2: return z;
    case 3: return o;
    default:
      LOG(FATAL) << "Bad";
      return x;
    }
  }

  const BigVecQ3 &operator[](int i) const {
    switch (i) {
    case 0: return x;
    case 1: return y;
    case 2: return z;
    case 3: return o;
    default:
      LOG(FATAL) << "Bad";
      return x;
    }
  }
};

// Scale the vector so that it has integer coordinates. The
// result is a canonical representation of the direction.
BigVecQ3 ScaleToMakeIntegral(const BigVecQ3 &a);

// TODO: Either make these members of BigVecQ* or make big-vec-overloads.h.

inline BigVecQ3 operator -(const BigVecQ3 &a) {
  return BigVecQ3(-a.x, -a.y, -a.z);
}

inline BigVecQ2 operator *(const BigRat &s, const BigVecQ2 &v) {
  return BigVecQ2(v.x * s, v.y * s);
}

inline BigQuat operator *(const BigQuat &q, const BigRat &s) {
  return BigQuat(q.x * s, q.y * s, q.z * s, q.w * s);
}

// Exact equality.
inline bool operator ==(const BigVecQ2 &a, const BigVecQ2 &b) {
  return a.x == b.x && a.y == b.y;
}

inline bool operator ==(const BigVecQ3 &a, const BigVecQ3 &b) {
  return a.x == b.x && a.y == b.y && a.z == b.z;
}

inline bool operator ==(const BigQuat &a, const BigQuat &b) {
  return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
}

inline BigRat dot(const BigVecQ2 &a, const BigVecQ2 &b) {
  return a.x * b.x + a.y * b.y;
}

inline BigRat dot(const BigVecQ3 &a, const BigVecQ3 &b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline BigRat cross(const BigVecQ2 &a, const BigVecQ2 &b) {
  return a.x * b.y - a.y * b.x;
}

inline BigVecQ3 cross(const BigVecQ3 &a, const BigVecQ3 &b) {
  return BigVecQ3{
    a.y * b.z - a.z * b.y,
    a.z * b.x - a.x * b.z,
    a.x * b.y - a.y * b.x
  };
}

inline quat4 SmallQuat(const BigQuat &q) {
  return quat4{q.x.ToDouble(), q.y.ToDouble(), q.z.ToDouble(), q.w.ToDouble()};
}

inline BigRat length_squared(const BigVecQ2 &a) {
  return dot(a, a);
}

inline BigRat length_squared(const BigVecQ3 &a) {
  return dot(a, a);
}

inline BigVecQ3 operator *(const BigRat &s, const BigVecQ3 &v) {
  return BigVecQ3{s * v.x, s * v.y, s * v.z};
}

template<>
struct Hashing<BigVecQ3> {
  std::size_t operator()(const BigVecQ3 &v) const {
    uint64_t x = BigRat::HashCode(v.x);
    uint64_t y = BigRat::HashCode(v.y);
    uint64_t z = BigRat::HashCode(v.z);
    return ((x * 3) + y) * 7 + z;
  }
};

// With color
std::string VecString(const BigVecQ2 &v);
std::string VecString(const BigVecQ3 &v);
std::string QuatString(const BigQuat &q);
// For serialization to disk, etc.
std::string PlainVecString(const BigVecQ2 &v);
std::string PlainQuatString(const BigQuat &q);

std::string FrameString(const BigFrame &f);

std::string FormatNum(const BigInt &b);

BigQuat Normalize(const BigQuat &q, int digits);

inline BigQuat Conjugate(const BigQuat &q) {
  return BigQuat(-q.x, -q.y, -q.z, q.w);
}

inline BigQuat operator*(const BigQuat &a, const BigQuat &b) {
  return BigQuat{
    a.x * b.w + a.w * b.x + a.y * b.z - a.z * b.y,
    a.y * b.w + a.w * b.y + a.z * b.x - a.x * b.z,
    a.z * b.w + a.w * b.z + a.x * b.y - a.y * b.x,
    a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
  };
}

inline vec3 SmallVec(const BigVecQ3 &v) {
  return vec3(v.x.ToDouble(), v.y.ToDouble(), v.z.ToDouble());
}

inline vec2 SmallVec(const BigVecQ2 &v) {
  return vec2(v.x.ToDouble(), v.y.ToDouble());
}

inline frame3 SmallFrame(const BigFrame &frame) {
  return frame3{
    .x = SmallVec(frame.x),
    .y = SmallVec(frame.y),
    .z = SmallVec(frame.z),
    .o = SmallVec(frame.o),
  };
}

inline quat4 quat_conjugate(const quat4 &q) {
  return {-q.x, -q.y, -q.z, q.w};
}

inline bool AllZero(const BigVecQ3 &v) {
  return BigRat::IsZero(v.x) && BigRat::IsZero(v.y) && BigRat::IsZero(v.z);
}

inline bool AllZero(const BigQuat &v) {
  return BigRat::IsZero(v.x) && BigRat::IsZero(v.y) && BigRat::IsZero(v.z) &&
    BigRat::IsZero(v.w);
}

// Compute the rigid frame that rotates according to the unit
// quaternion v.
BigFrame RotationFrame(const BigQuat &v);

// Same, but not requiring a unit quaternion.
BigFrame NonUnitRotationFrame(const BigQuat &v);

// Returns a rational position on the view sphere,
// with an unspecified rotation around that axis.
// The quat does not need to be unit-length.
//
// This view position is such that applying the rotation
// described by the quaternion moves the view position
// *to* the Z axis (0, 0, 1). Be careful not to mix this
// up with a rotation *from* the Z axis to the position.
BigVecQ3 ViewPosFromNonUnitQuat(const BigQuat &q);

inline BigVecQ3 TransformPoint(const BigFrame &f, const BigVecQ3 &v) {
  return f.x * v.x + f.y * v.y + f.z * v.z + f.o;
}

inline BigVecQ2 TransformAndProjectPoint(const BigFrame &f, const BigVecQ3 &v) {
  // scale vector, but discard the z coordinate
  auto Times3To2 = [](const BigVecQ3 &u, const BigRat &r) {
    return BigVecQ2(u.x * r, u.y * r);
  };

  BigVecQ2 fx = Times3To2(f.x, v.x);
  BigVecQ2 fy = Times3To2(f.y, v.y);
  BigVecQ2 fz = Times3To2(f.z, v.z);
  // PERF this is always zero for our problems
  BigVecQ2 o = BigVecQ2(f.o.x, f.o.y);

  return fx + fy + fz + o;
}

// Small Fixed-size matrices stored in column major format.
struct BigMat3 {
  // left column
  BigVecQ3 x = {BigRat(1), BigRat(0), BigRat(0)};
  // middle column
  BigVecQ3 y = {BigRat(0), BigRat(1), BigRat(0)};
  // right column
  BigVecQ3 z = {BigRat(0), BigRat(0), BigRat(1)};

  BigVecQ3 &operator[](int i) {
    switch (i) {
    case 0: return x;
    case 1: return y;
    case 2: return z;
    default:
      LOG(FATAL) << "Index out of bounds.";
    }
  }

  const BigVecQ3 &operator[](int i) const {
    switch (i) {
    case 0: return x;
    case 1: return y;
    case 2: return z;
    default:
      LOG(FATAL) << "Index out of bounds.";
    }
  }
};

inline BigMat3 Rotation(const BigFrame &a) {
  return BigMat3{a.x, a.y, a.z};
}

inline BigMat3 Transpose(const BigMat3 &a) {
  return BigMat3{
    .x = BigVecQ3{a.x.x, a.y.x, a.z.x},
    .y = BigVecQ3{a.x.y, a.y.y, a.z.y},
    .z = BigVecQ3{a.x.z, a.y.z, a.z.z},
  };
}

inline BigVecQ3 operator*(const BigMat3 &a, const BigVecQ3 &b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline BigFrame InverseRigid(const BigFrame &a) {
  BigMat3 mtx = Transpose(Rotation(a));
  BigFrame ret;
  return BigFrame{
    .x = mtx.x, .y = mtx.y, .z = mtx.z,
    .o = -(mtx * a.o),
  };
}


// XXX These don't work how I'd expect. Fix or delete.
// (it was likely the quat operator* bug?)
BigVecQ3 RotatePoint(const BigQuat &q, const BigVecQ3 &v);
BigPoly Rotate(const BigQuat &q, const BigPoly &poly);

// Rotate the entire polyhedron using the frame. Exact.
BigPoly Rotate(const BigFrame &f, const BigPoly &poly);
// Project to 2D along the z axis.
BigMesh2D Shadow(const BigPoly &poly);
// Translate the entire mesh by the vector t.
BigMesh2D Translate(const BigVecQ2 &t, const BigMesh2D &m);
// Rotate and project at once, which is faster because we don't
// need to compute the discarded z coordinates.
BigMesh2D RotateAndProject(const BigFrame &f, const BigPoly &poly);

Polyhedron SmallPoly(const BigPoly &big);
PolyhedronMesh2D SmallMesh(const BigMesh2D &big);

// Positive if clockwise winding order; negative for ccw.
BigRat SignedAreaOfHull(const BigMesh2D &mesh, const std::vector<int> &hull);
// Same, but with the points directly.
BigRat SignedAreaOfConvexPoly(const std::vector<BigVecQ2> &points);


BigPoly MakeBigPolyFromVertices(std::vector<BigVecQ3> vertices);

// Some polyhedra with arbitrary precision.
BigPoly BigRidode(int digits);
BigPoly BigDhexe(int digits);
BigPoly BigPhexe(int digits);
BigPoly BigScube(int digits);
BigPoly BigSdode(int digits);
BigPoly BigTriac(int digits);
// Some are exact, but we take an (ignored) digits argument anyway.
BigPoly BigCube(int digits);
BigPoly BigTetra(int digits);

// Returns true if the solution (doubles) is actually valid (using
// rational arithmetic with the specified precision).
//
// Note that this does not ensure that the frame is actually a
// rigid transformation, and it is likely not quite one due to
// floating point error.
bool ValidateSolution(const BigPoly &poly,
                      const frame3 &outer,
                      const frame3 &inner,
                      int digits);

// Point-in-polygon test using the winding number algorithm.
// Takes a vertex buffer and indices into that set.
bool PointInPolygon(const std::vector<BigVecQ2> &vertices,
                    const std::vector<int> &polygon,
                    const BigVecQ2 &point);

// Takes the polygon directly as vertices.
bool PointInPolygon(const std::vector<BigVecQ2> &polygon,
                    const BigVecQ2 &point);

// Is pt strictly within the triangle a-b-c? Exact. Works with both
// winding orders.
bool InTriangle(const BigVecQ2 &a, const BigVecQ2 &b, const BigVecQ2 &c,
                const BigVecQ2 &pt);

// Is the point strictly within any triangle in the mesh? Note that this
// excludes points that are inside the hull but only lie exactly on
// edges, like the center of a triangulated square. This is often not
// what you want! TODO: Fix!
bool InMesh(const BigMesh2D &mesh, const BigVecQ2 &pt);

// Check if the point is strictly within *any* triangle induced by the
// point set. Returns such a triangle if so. This also can exclude a
// point that lies exactly and exclusively on internal edges (e.g. the
// center of a triangulated square), although most of the time there
// will then be another triangle that includes it.
std::optional<std::tuple<int, int, int>>
InMeshExhaustive(const BigMesh2D &mesh, const BigVecQ2 &pt);

inline BigRat distance_squared(const BigVecQ2 &a, const BigVecQ2 &b) {
  BigVecQ2 edge(a.x - b.x, a.y - b.y);
  return length_squared(edge);
}

// Get the index of the closest mesh point to the target point.
int GetClosestPoint(const BigMesh2D &mesh, const BigVecQ2 &pt);

// Get the vertex index of the closest vertex (among those in the
// hull) to the target point. Also returns the *squared* distance.
std::pair<int, BigRat> GetClosestPoint(const std::vector<BigVecQ2> &vertices,
                                       const std::vector<int> &hull,
                                       const BigVecQ2 &pt);

// Get the convex hull. This is intended to be exact.
std::vector<int> BigQuickHull(const std::vector<BigVecQ2> &vertices);
// Fast(er), but approximate since it uses the double-based
// hull computation.
std::vector<int> BigHull(const std::vector<BigVecQ2> &bigvs);

// Check if the point is strictly within the convex hull. Either hull
// orientation works, but it must be a convex polygon (not just a
// point set).
bool InHull(const std::vector<BigVecQ2> &vertices,
            const std::vector<int> &hull,
            const BigVecQ2 &pt);

BigRat SquaredDistanceToClosestPointOnSegment(
    // Line segment
    const BigVecQ2 &v0,
    const BigVecQ2 &v1,
    // Point to test
    const BigVecQ2 &pt);

BigRat SquaredDistanceToHull(const std::vector<BigVecQ2> &vertices,
                             const std::vector<int> &hull,
                             const BigVecQ2 &pt);

#endif
