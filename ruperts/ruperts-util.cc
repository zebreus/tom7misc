
#include "ruperts-util.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <format>
#include <limits>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "ansi.h"
#include "arcfour.h"
#include "base/print.h"
#include "base/stringprintf.h"
#include "geom/hull-2d.h"
#include "geom/hull-3d.h"
#include "geom/mesh.h"
#include "geom/polyhedra.h"
#include "hashing.h"
#include "randutil.h"
#include "util.h"
#include "yocto-math.h"

using Mesh2D = PolyhedronMesh2D;

std::string FormatNum(uint64_t n) {
  if (n > 1'000'000) {
    double m = n / 1'000'000.0;
    if (m >= 1'000'000.0) {
      return std::format("{:.1f}T", m / 1'000'000.0);
    } else if (m >= 1000.0) {
      return std::format("{:.1f}B", m / 1000.0);
    } else if (m >= 100.0) {
      return std::format("{}M", (int)std::round(m));
    } else if (m > 10.0) {
      return std::format("{:.1f}M", m);
    } else {
      // TODO: Integer division. color decimal place and suffix.
      return std::format("{:.2f}M", m);
    }
  } else {
    return Util::UnsignedWithCommas(n);
  }
}

vec3 ViewPosFromNonUnitQuat(const quat4 &q) {
  double xx = q.x * q.x;
  double yy = q.y * q.y;
  double zz = q.z * q.z;
  double ww = q.w * q.w;

  double two_s = 2.0 / (xx + yy + zz + ww);

  double zx = q.z * q.x;
  double yw = q.y * q.w;
  double yz = q.y * q.z;
  double xw = q.x * q.w;

  return vec3(two_s * (zx - yw),
              two_s * (yz + xw),
              1.0 - two_s * (xx + yy));
}

std::pair<quat4, vec3> UnpackFrame(const frame3 &f) {
  using mat3 = yocto::mat<double, 3>;

  const mat3 m = rotation(f);

  double w = sqrt(std::max(0.0, 1.0 + m[0][0] + m[1][1] + m[2][2])) * 0.5;
  double x = sqrt(std::max(0.0, 1.0 + m[0][0] - m[1][1] - m[2][2])) * 0.5;
  double y = sqrt(std::max(0.0, 1.0 - m[0][0] + m[1][1] - m[2][2])) * 0.5;
  double z = sqrt(std::max(0.0, 1.0 - m[0][0] - m[1][1] + m[2][2])) * 0.5;

  if (m[1][2] - m[2][1] < 0.0) x = -x;
  if (m[2][0] - m[0][2] < 0.0) y = -y;
  if (m[0][1] - m[1][0] < 0.0) z = -z;

  return std::make_pair(normalize(quat4(x, y, z, w)),
                        yocto::translation(f));
}

// via https://en.wikipedia.org/wiki/Shoelace_formula
double SignedAreaOfHull(const std::vector<vec2> &vertices,
                        const std::vector<int> &hull) {
  if (hull.size() < 3) return 0.0;
  double area = 0.0;
  // Iterate through the polygon vertices, using the shoelace formula.
  for (size_t i = 0; i < hull.size(); i++) {
    const vec2 &v0 = vertices[hull[i]];
    const vec2 &v1 = vertices[hull[(i + 1) % hull.size()]];
    area += v0.x * v1.y - v1.x * v0.y;
  }

  return area * 0.5;
}

double SignedAreaOfHull(const Mesh2D &mesh, const std::vector<int> &hull) {
  return SignedAreaOfHull(mesh.vertices, hull);
}

double AreaOfHull(const std::vector<vec2> &vertices,
                  const std::vector<int> &hull) {
  // Sign depends on the winding order, but we always want a positive
  // area.
  return std::abs(SignedAreaOfHull(vertices, hull));
}

double AreaOfHull(const Mesh2D &mesh, const std::vector<int> &hull) {
  return AreaOfHull(mesh.vertices, hull);
}

quat4 RotationFromAToB(const vec3 &a, const vec3 &b) {
  vec3 norma = normalize(a);
  vec3 normb = normalize(b);
  double d = dot(norma, normb);
  vec3 axis = cross(norma, normb);
  if (length_squared(axis) < 1e-10) {
    if (d > 0) {
      return quat4{0, 0, 0, 1};
    } else {
      // Rotate around any perpendicular axis.
      vec3 perp_axis = orthogonal(norma);
      return QuatFromVec(yocto::rotation_quat(perp_axis, std::numbers::pi));
    }
  }

  double angle = std::acos(std::clamp(d, -1.0, 1.0));
  return QuatFromVec(yocto::rotation_quat(axis, angle));

  // TODO: We should be able to do this without the special cases?
#if 0
  double d = dot(a, b);
  vec3 axis = cross(a, b);

  double s = sqrt((1.0 + d) * 2.0);
  double inv_s = 1.0 / s;
  return normalize(quat4(axis.x * inv_s, axis.y * inv_s, axis.z * inv_s,
                         s * 0.5));
#endif
}

std::optional<double> GetRatio(const Polyhedron &poly,
                               const frame3 &outer_frame,
                               const frame3 &inner_frame) {
  // Compute new error ratio.
  Polyhedron outer = Rotate(poly, outer_frame);
  Polyhedron inner = Rotate(poly, inner_frame);
  Mesh2D souter = Shadow(outer);
  Mesh2D sinner = Shadow(inner);

  if (AllZero(souter.vertices) ||
      AllZero(sinner.vertices)) {
    /*
    fprintf(stderr, "Outer:\n%s\nInner:\n%s\n",
            FrameString(outer_frame).c_str(),
            FrameString(inner_frame).c_str());
    LOG(FATAL) << "???";
    */
    return std::nullopt;
  }

  std::vector<int> outer_hull = Hull2D::QuickHull(souter.vertices);
  std::vector<int> inner_hull = Hull2D::QuickHull(sinner.vertices);

  for (const vec2 &iv : sinner.vertices) {
    if (!InHull(souter, outer_hull, iv)) {
      return std::nullopt;
    }
  }

  double outer_area = AreaOfHull(souter, outer_hull);
  double inner_area = AreaOfHull(sinner, inner_hull);

  double ratio = inner_area / outer_area;
  if (std::isfinite(ratio) && ratio > 0.0) {
    return {ratio};
  } else {
    return std::nullopt;
  }
}

std::optional<double> GetClearance(const Polyhedron &poly,
                                   const frame3 &outer_frame,
                                   const frame3 &inner_frame) {
  Polyhedron outer = Rotate(poly, outer_frame);
  Polyhedron inner = Rotate(poly, inner_frame);
  Mesh2D souter = Shadow(outer);
  Mesh2D sinner = Shadow(inner);

  if (AllZero(souter.vertices) ||
      AllZero(sinner.vertices)) {
    /*
    fprintf(stderr, "Outer:\n%s\nInner:\n%s\n",
            FrameString(outer_frame).c_str(),
            FrameString(inner_frame).c_str());
    LOG(FATAL) << "???";
    */
    return std::nullopt;
  }

  std::vector<int> outer_hull = Hull2D::QuickHull(souter.vertices);
  std::vector<int> inner_hull = Hull2D::QuickHull(sinner.vertices);

  for (const vec2 &iv : sinner.vertices) {
    if (!InHull(souter, outer_hull, iv)) {
      return std::nullopt;
    }
  }

  double c = HullClearance(souter.vertices, outer_hull,
                           sinner.vertices, inner_hull);
  if (std::isfinite(c) && c > 0.0) {
    return {c};
  } else {
    return std::nullopt;
  }
}


std::pair<int, int> TwoNonParallelFaces(ArcFour *rc, const Polyhedron &poly) {
  const int num_faces = (int)poly.faces->v.size();
  for (;;) {
    int f1 = RandTo(rc, num_faces);
    int f2 = RandTo(rc, num_faces);
    if (!FacesParallel(poly, f1, f2)) {
      return std::make_pair(f1, f2);
    }
  }
}

std::vector<HalfSpace> ExtractHalfSpacesFromHull(
    const std::vector<vec3> &vertices) {
  if (vertices.size() < 4) return {};

  auto triangles = Hull3D::HullFaces(vertices);
  std::vector<HalfSpace> planes;

  // Compute centroid of vertices to orient normals outward from interior.
  vec3 centroid = vec3(0, 0, 0);
  for (const vec3 &v : vertices) {
    centroid += v;
  }
  centroid = centroid / (double)vertices.size();

  for (const auto &[i, j, k] : triangles) {
    const vec3 &v0 = vertices[i];
    const vec3 &v1 = vertices[j];
    const vec3 &v2 = vertices[k];

    vec3 cross_prod = yocto::cross(v1 - v0, v2 - v0);
    double len = yocto::length(cross_prod);
    if (len < 1e-12) continue;

    vec3 normal = cross_prod / len;
    double d = yocto::dot(normal, v0);

    // Ensure normal points outward: centroid is strictly in interior,
    // so dot(normal, centroid) < d must hold.
    if (yocto::dot(normal, centroid) > d) {
      normal = -normal;
      d = -d;
    }

    // Deduplicate near-identical planes
    bool duplicate = false;
    for (const auto &existing : planes) {
      if (yocto::length(existing.normal - normal) < 1e-5 &&
          std::abs(existing.d - d) < 1e-5) {
        duplicate = true;
        break;
      }
    }
    if (!duplicate) {
      planes.push_back(HalfSpace{.normal = normal, .d = d});
    }
  }

  return planes;
}

double HullClearance(const std::vector<vec2> &outer_points,
                     const std::vector<int> &outer_hull,
                     const std::vector<vec2> &inner_points,
                     const std::vector<int> &inner_hull) {
  // The minimum distance must be between a vertex on one and
  // an edge on the other.

  double min_sqdist = std::numeric_limits<double>::infinity();
  for (int i = 0; i < inner_hull.size(); i++) {
    const vec2 &i1 = inner_points[inner_hull[i]];
    const vec2 &i2 = inner_points[inner_hull[(i + 1) % inner_hull.size()]];
    for (int o = 0; o < outer_hull.size(); o++) {
      const vec2 &o1 = outer_points[outer_hull[o]];
      const vec2 &o2 = outer_points[outer_hull[(o + 1) % outer_hull.size()]];

      double di = SquaredPointLineDistance(i1, i2, o1);
      double ii = SquaredPointLineDistance(o1, o2, i1);
      min_sqdist = std::min(min_sqdist, std::min(di, ii));
    }
  }

  return std::sqrt(min_sqdist);
}

std::vector<PolygonEdge> GetHullEdges(std::span<const vec2> verts,
                                     const std::vector<int> &outer_hull) {
  std::vector<PolygonEdge> edges;
  const int m = outer_hull.size();
  edges.reserve(m);
  vec2 centroid = vec2{0, 0};
  for (int idx : outer_hull)
    centroid += verts[idx];
  if (m > 0)
    centroid /= (double)m;

  for (int i = 0; i < m; i++) {
    const vec2 p1 = verts[outer_hull[i]];
    const vec2 p2 = verts[outer_hull[(i + 1) % m]];
    const vec2 d = p2 - p1;
    const double len = length(d);
    if (len < 1e-12)
      continue;
    vec2 n = vec2{-d.y / len, d.x / len};
    double b = dot(n, p1);
    if (dot(n, centroid) < b) {
      n = -n;
      b = -b;
    }
    edges.push_back({n, b});
  }
  return edges;
}

std::vector<PolygonEdge> GetHullEdges(const PolyhedronMesh2D &souter,
                                     const std::vector<int> &outer_hull) {
  return GetHullEdges(std::span<const vec2>(souter.vertices), outer_hull);
}

static inline double EvalClearanceMargin(const std::vector<PolygonEdge> &edges,
                                        const double *d,
                                        const vec2 &t) {
  double min_m = std::numeric_limits<double>::infinity();
  for (size_t j = 0; j < edges.size(); j++) {
    double m = dot(edges[j].normal, t) - d[j];
    if (m < min_m)
      min_m = m;
  }
  return min_m;
}

Clearance2D MaximizeClearance2D(const std::vector<PolygonEdge> &edges,
                                std::span<const vec2> inner_verts,
                                vec2 initial_translation, double initial_step) {
  const int m = edges.size();
  double d_buf[64];
  std::vector<double> d_vec;
  double *d = d_buf;
  if (m > 64) {
    d_vec.resize(m);
    d = d_vec.data();
  }

  for (int j = 0; j < m; j++) {
    double min_proj = std::numeric_limits<double>::infinity();
    for (const vec2 &v : inner_verts) {
      double proj = dot(edges[j].normal, v);
      if (proj < min_proj)
        min_proj = proj;
    }
    d[j] = edges[j].b - min_proj;
  }

  // 2D Nelder-Mead simplex optimization on R^2.
  double step = initial_step;
  vec2 p[3] = {initial_translation, initial_translation + vec2{step, 0.0},
               initial_translation + vec2{0.0, step}};
  double val[3];
  for (int i = 0; i < 3; i++)
    val[i] = EvalClearanceMargin(edges, d, p[i]);

  for (int iter = 0; iter < 60; iter++) {
    if (val[1] > val[0]) {
      std::swap(p[0], p[1]);
      std::swap(val[0], val[1]);
    }
    if (val[2] > val[0]) {
      std::swap(p[0], p[2]);
      std::swap(val[0], val[2]);
    }
    if (val[2] > val[1]) {
      std::swap(p[1], p[2]);
      std::swap(val[1], val[2]);
    }

    vec2 c = (p[0] + p[1]) * 0.5;
    vec2 xr = c + (c - p[2]);
    double vr = EvalClearanceMargin(edges, d, xr);

    if (vr > val[0]) {
      vec2 xe = c + (xr - c) * 2.0;
      double ve = EvalClearanceMargin(edges, d, xe);
      if (ve > vr) {
        p[2] = xe;
        val[2] = ve;
      } else {
        p[2] = xr;
        val[2] = vr;
      }
    } else if (vr > val[1]) {
      p[2] = xr;
      val[2] = vr;
    } else if (vr > val[2]) {
      // Outside contraction: reflection is better than worst
      vec2 xc = c + (xr - c) * 0.5;
      double vc = EvalClearanceMargin(edges, d, xc);
      if (vc >= vr) {
        p[2] = xc;
        val[2] = vc;
      } else {
        p[2] = xr;
        val[2] = vr;
      }
    } else {
      // Inside contraction: reflection is worse than worst
      vec2 xc = c + (p[2] - c) * 0.5;
      double vc = EvalClearanceMargin(edges, d, xc);
      if (vc > val[2]) {
        p[2] = xc;
        val[2] = vc;
      } else {
        // Shrink toward p[0]
        p[1] = p[0] + (p[1] - p[0]) * 0.5;
        val[1] = EvalClearanceMargin(edges, d, p[1]);
        p[2] = p[0] + (p[2] - p[0]) * 0.5;
        val[2] = EvalClearanceMargin(edges, d, p[2]);
      }
    }
  }

  const int best = (val[1] > val[0]) ?
    (val[2] > val[1] ? 2 : 1) : (val[2] > val[0] ? 2 : 0);
  return Clearance2D{.clearance = val[best], .translation = p[best]};
}

Clearance2D FastSilhouetteClearance(const std::vector<PolygonEdge> &outer_edges,
                                    const Polyhedron &poly,
                                    const frame3 &inner_rot_frame,
                                    vec2 initial_translation) {
  Polyhedron inner = Rotate(poly, inner_rot_frame);
  PolyhedronMesh2D sinner = Shadow(inner);
  return MaximizeClearance2D(outer_edges, sinner.vertices, initial_translation);
}

// PERF: See polyehdra_benchmark for different approaches. This
// was the winner for the snub cube, but the tradeoffs are likely
// different for other shapes. (In particular, QuickHull may be
// a better choice for large numbers of vertices, and computing
// the hull/hullcircle is probably pointless for something like
// the tetrahedron).
double HeteroLossFunctionContainsOrigin(const Polyhedron &outer_poly,
                                        const Polyhedron &inner_poly,
                                        const frame3 &outer_frame,
                                        const frame3 &inner_frame) {
  Mesh2D souter = Shadow(Rotate(outer_poly, outer_frame));
  Mesh2D sinner = Shadow(Rotate(inner_poly, inner_frame));

  // Although computing the convex hull is expensive, the tests
  // below are O(n*m), so it is helpful to significantly reduce
  // one of the factors.
  const std::vector<int> outer_hull = Hull2D::GrahamScan(souter.vertices);
  if (outer_hull.size() < 3) {
    // If the outer hull is degenerate, then the inner hull
    // cannot be strictly within it. We don't have a good
    // way to measure the gradient here, though.
    return 1'000'000.0;
  }

  HullInscribedCircle circle(souter.vertices, outer_hull);

  // Does every vertex in inner fall inside the outer shadow?
  double error = 0.0;
  int errors = 0;
  for (const vec2 &iv : sinner.vertices) {
    if (circle.DefinitelyInside(iv))
      continue;

    if (!InHull(souter, outer_hull, iv)) {
      // slow :(
      error += DistanceToHull(souter.vertices, outer_hull, iv);
      errors++;
    }
  }

  if (error == 0.0 && errors > 0) [[unlikely]] {
    // If they are not in the mesh, don't return an actual zero.
    return std::numeric_limits<double>::min() * errors;
  } else {
    return error;
  }
}

double LossFunctionContainsOrigin(const Polyhedron &poly,
                                  const frame3 &outer_frame,
                                  const frame3 &inner_frame) {
  return HeteroLossFunctionContainsOrigin(poly, poly,
                                          outer_frame, inner_frame);
}

double LossFunction(const Polyhedron &poly,
                    const frame3 &outer_frame,
                    const frame3 &inner_frame) {
  Mesh2D souter = Shadow(Rotate(poly, outer_frame));
  Mesh2D sinner = Shadow(Rotate(poly, inner_frame));

  // Although computing the convex hull is expensive, the tests
  // below are O(n*m), so it is helpful to significantly reduce
  // one of the factors.
  const std::vector<int> outer_hull = Hull2D::GrahamScan(souter.vertices);
  if (outer_hull.size() < 3) {
    // If the outer hull is degenerate, then the inner hull
    // cannot be strictly within it. We don't have a good
    // way to measure the gradient here, though.
    return 1'000'000.0;
  }

  // Does every vertex in inner fall inside the outer shadow?
  double error = 0.0;
  int errors = 0;
  for (const vec2 &iv : sinner.vertices) {
    if (!InHull(souter, outer_hull, iv)) {
      // slow :(
      error += DistanceToHull(souter.vertices, outer_hull, iv);
      errors++;
    }
  }

  if (error == 0.0 && errors > 0) [[unlikely]] {
    // If they are not in the mesh, don't return an actual zero.
    return std::numeric_limits<double>::min() * errors;
  } else {
    return error;
  }
}

double FullLossContainsOrigin(
    const Polyhedron &poly,
    const frame3 &outer_frame, const frame3 &inner_frame) {

  Polyhedron outer = Rotate(poly, outer_frame);
  Polyhedron inner = Rotate(poly, inner_frame);
  Mesh2D souter = Shadow(outer);
  Mesh2D sinner = Shadow(inner);

  if (AllZero(souter.vertices) ||
      AllZero(sinner.vertices)) {

    return 1.0e6;
  }

  std::vector<int> outer_hull = Hull2D::QuickHull(souter.vertices);

  if (outer_hull.size() < 3) {
    return 1.0e6;
  }

  HullInscribedCircle circle(souter.vertices, outer_hull);

  // Does every vertex in inner fall inside the outer shadow?
  double error = 0.0;
  int errors = 0;
  for (const vec2 &iv : sinner.vertices) {
    if (circle.DefinitelyInside(iv))
      continue;

    if (!InHull(souter, outer_hull, iv)) {
      // slow :(
      error += DistanceToHull(souter.vertices, outer_hull, iv);
      errors++;
    }
  }

  if (errors > 0) {
    if (error == 0.0) {
      [[unlikely]]
      return std::numeric_limits<double>::min() * errors;
    }
    return error;
  } else {
    std::vector<int> inner_hull = Hull2D::QuickHull(sinner.vertices);
    double clearance = HullClearance(souter.vertices, outer_hull,
                                     sinner.vertices, inner_hull);
    return std::min(-clearance, 0.0);
  }
}

TriangularMesh3D ApproximateSphere(int depth) {

  #if 0
  // Start with tetrahedron.
  // You get a cool looking shape, but it's actually pretty
  // irregular.
  TriangularMesh3D mesh;
  mesh.vertices = {
    normalize(vec3{1.0,   1.0,  1.0}),
    normalize(vec3{1.0,  -1.0, -1.0}),
    normalize(vec3{-1.0,  1.0, -1.0}),
    normalize(vec3{-1.0, -1.0,  1.0}),
  };

  for (int i = 0; i < 4; i++) {
    for (int j = i + 1; j < 4; j++) {
      for (int k = j + 1; k < 4; k++) {
        mesh.triangles.emplace_back(i, j, k);
      }
    }
  }
  #endif

  // Icosahedron is way better!
  TriangularMesh3D mesh = []() {
      Polyhedron icos = Icosahedron();
      TriangularMesh3D mesh{
        .vertices = icos.vertices,
        .triangles = icos.faces->triangulation,
      };

      for (vec3 &v : mesh.vertices) {
        v = normalize(v);
      }

      OrientMesh(&mesh);

      return mesh;
    }();

  // Triforce Subdivision.
  while (depth--) {
    std::unordered_map<std::pair<int, int>, int,
                       Hashing<std::pair<int, int>>> midpoints;
    TriangularMesh3D submesh;
    submesh.vertices = mesh.vertices;

    auto MidPoint = [&](int a, int b) {
        if (a > b) std::swap(a, b);
        auto it = midpoints.find(std::make_pair(a, b));
        if (it == midpoints.end()) {
          CHECK(b < mesh.vertices.size());
          // We want the average, but since we are normalizing anyway,
          // we can skip the scale.
          vec3 m = normalize(mesh.vertices[a] + mesh.vertices[b]);
          int id = submesh.vertices.size();
          midpoints[std::make_pair(a, b)] = id;
          submesh.vertices.push_back(m);
          return id;
        }
        else return it->second;
      };

    for (const auto &[a, b, c] : mesh.triangles) {
      //
      //    a---d---b
      //     \ / \ /
      //      e---f
      //       \ /
      //        c
      //
      int d = MidPoint(a, b);
      int e = MidPoint(a, c);
      int f = MidPoint(b, c);

      // Preserve clockwise winding.
      submesh.triangles.emplace_back(a, d, e);
      submesh.triangles.emplace_back(d, b, f);
      submesh.triangles.emplace_back(d, f, e);
      submesh.triangles.emplace_back(e, f, c);
    }
    mesh = std::move(submesh);
  }

  return mesh;
}

TriangularMesh3D PolyToTriangularMesh(const Polyhedron &poly) {
  return TriangularMesh3D{.vertices = poly.vertices,
    .triangles = poly.faces->triangulation};
}

void SaveAsSTL(const Polyhedron &poly, std::string_view filename) {
  TriangularMesh3D mesh = PolyToTriangularMesh(poly);
  OrientMesh(&mesh);
  return SaveAsSTL(mesh, filename, poly.name);
}

void SaveSolutionAsJSON(const frame3 &outer_frame,
                        const frame3 &inner_frame,
                        std::string_view filename) {
  std::string contents = "{\n";
  AppendFormat(
      &contents,
      " \"outerframe\": "
      "[\n  {},{},{},\n  {},{},{},\n  {},{},{},\n  {},{},{}],",
      outer_frame.x.x, outer_frame.x.y, outer_frame.x.z,
      outer_frame.y.x, outer_frame.y.y, outer_frame.y.z,
      outer_frame.z.x, outer_frame.z.y, outer_frame.z.z,
      outer_frame.o.x, outer_frame.o.y, outer_frame.o.z);
  AppendFormat(
      &contents,
      " \"innerframe\": "
      "[\n  {},{},{},\n  {},{},{},\n  {},{},{},\n  {},{},{}]",
      inner_frame.x.x, inner_frame.x.y, inner_frame.x.z,
      inner_frame.y.x, inner_frame.y.y, inner_frame.y.z,
      inner_frame.z.x, inner_frame.z.y, inner_frame.z.z,
      inner_frame.o.x, inner_frame.o.y, inner_frame.o.z);
  contents.append("\n}\n");

  std::string f = (std::string)filename;
  Util::WriteFile(f, contents);
  Print("Wrote " AGREEN("{}") "\n", f);
}

namespace {
struct NameMap {
  NameMap() : names(
      std::vector<std::tuple<std::string, std::string, std::string>>{
        {"tetra", "tetrahedron", "tetrahedron"},
        {"cube", "cube", "cube"},
        {"dode", "dodecahedron", "dodecahedron"},
        {"icos", "icosahedron", "icosahedron"},
        {"octa", "octahedron", "octahedron"},
        {"ttetra", "truncatedtetrahedron", "truncated tetrahedron"},
        {"cocta", "cuboctahedron", "cuboctahedron"},
        {"tcube", "truncatedcube", "truncated cube"},
        {"tocta", "truncatedoctahedron", "truncated octahedron"},
        {"rcocta", "rhombicuboctahedron", "rhombicuboctahedron"},
        {"tcocta", "truncatedcuboctahedron", "truncated cuboctahedron"},
        {"scube", "snubcube", "snub cube"},
        {"idode", "icosidodecahedron", "icosidodecahedron"},
        {"tdode", "truncateddodecahedron", "truncated dodecahedron"},
        {"ticos", "truncatedicosahedron", "truncated icosahedron"},
        {"ridode", "rhombicosidodecahedron", "rhombicosidodecahedron"},
        {"tidode", "truncatedicosidodecahedron", "truncated icosidodecahedron"},
        {"sdode", "snubdodecahedron", "snub dodecahedron"},
        {"ktetra", "triakistetrahedron", "triakis tetrahedron"},
        {"rdode", "rhombicdodecahedron", "rhombic dodecahedron"},
        {"kocta", "triakisoctahedron", "triakis octahedron"},
        {"thexa", "tetrakishexahedron", "tetrakis hexahedron"},
        {"ditet", "deltoidalicositetrahedron", "deltoidal icositetrahedron"},
        {"ddode", "disdyakisdodecahedron", "disdyakis dodecahedron"},
        {"dhexe", "deltoidalhexecontahedron", "deltoidal hexecontahedron"},
        {"pitet", "pentagonalicositetrahedron", "pentagonal icositetrahedron"},
        {"rtriac", "rhombictriacontahedron", "rhombic triacontahedron"},
        {"kicos", "triakisicosahedron", "triakis icosahedron"},
        {"pdode", "pentakisdodecahedron", "pentakis dodecahedron"},
        {"dtriac", "disdyakistriacontahedron", "disdyakis triacontahedron"},
        {"phexe", "pentagonalhexecontahedron", "pentagonal hexecontahedron"},

        {"nope", "noperthedron", "noperthedron"},
        {"onpe", "onperthedron", "onperthedron"},
      }) {}
  std::vector<std::tuple<std::string, std::string, std::string>> names;
};

struct DualMap {
  DualMap() : duals(
      std::vector<std::pair<std::string, std::string>>{
        {"tetrahedron", "tetrahedron"},
        {"cube", "octahedron"},
        {"dodecahedron", "icosahedron"},

        {"triakistetrahedron", "truncatedtetrahedron"},
        {"cuboctahedron", "rhombicdodecahedron"},
        {"truncatedcube", "triakisoctahedron"},
        {"tetrakishexahedron", "truncatedoctahedron"},
        {"rhombicuboctahedron", "deltoidalicositetrahedron"},
        {"snubcube", "pentagonalicositetrahedron"},
        {"rhombictriacontahedron", "icosidodecahedron"},
        {"truncatedcuboctahedron", "disdyakisdodecahedron"},
        {"truncateddodecahedron", "triakisicosahedron"},
        {"pentakisdodecahedron", "truncatedicosahedron"},
        {"rhombicosidodecahedron", "deltoidalhexecontahedron"},
        {"snubdodecahedron", "pentagonalhexecontahedron"},
        {"disdyakistriacontahedron", "truncatedicosidodecahedron"},
      }) {}
  // Every P/A/C polyhedron appears on at least one side.
  std::vector<std::pair<std::string, std::string>> duals;
};
}  // namespace

static const NameMap &GetNameMap() {
  static const NameMap *m = new NameMap;
  return *m;
}

static const DualMap &GetDualMap() {
  static const DualMap *m = new DualMap;
  return *m;
}

std::string DualPolyhedron(std::string_view name) {
  for (const auto &[a, b] : GetDualMap().duals) {
    if (name == a) return b;
    if (name == b) return a;
  }
  LOG(FATAL) << "Bad polyhedron name or missing from dual map: " << name;
  return "";
}

std::string PolyhedronShortName(std::string_view name) {
  for (const auto &[a, b, c] : GetNameMap().names) {
    if (b == name) return a;
  }
  LOG(FATAL) << "Unknown polyhedron identifier: " << name;
}

std::string PolyhedronIdFromNickname(std::string_view name) {
  for (const auto &[a, b, c] : GetNameMap().names) {
    if (a == name) return b;
  }
  LOG(FATAL) << "Unknown polyhedron nickname: " << name;
}

std::string PolyhedronHumanName(std::string_view name) {
  for (const auto &[a, b, c] : GetNameMap().names) {
    if (b == name) return c;
  }
  LOG(FATAL) << "Unknown polyhedron identifier: " << name;
}

const std::unordered_set<std::string> &Wishlist() {
  static auto *s = new std::unordered_set<std::string>{
    "snubcube",
    "rhombicosidodecahedron",
    "snubdodecahedron",
    "pentagonalhexecontahedron",
    "deltoidalhexecontahedron",
  };

  return *s;
}
