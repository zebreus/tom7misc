
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <functional>
#include <initializer_list>
#include <mutex>
#include <numbers>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "ansi.h"
#include "arcfour.h"
#include "atomic-util.h"
#include "auto-histo.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "hashing.h"
#include "image.h"
#include "mov-recorder.h"
#include "opt/opt.h"
#include "periodically.h"
#include "randutil.h"
#include "set-util.h"
#include "status-bar.h"
#include "threadutil.h"
#include "timer.h"
#include "util.h"

#include "yocto_matht.h"

DECLARE_COUNTERS(iters, attempts, u1_, u2_, u3_, u4_, u5_, u6_);

using vec2 = yocto::vec<double, 2>;
using vec3 = yocto::vec<double, 3>;
using vec4 = yocto::vec<double, 4>;
using mat4 = yocto::mat<double, 4>;
using quat4 = yocto::quat<double, 4>;
using frame3 = yocto::frame<double, 3>;

[[maybe_unused]]
static inline vec4 VecFromQuat(const quat4 &q) {
  return vec4{.x = q.x, .y = q.y, .z = q.z, .w = q.w};
}

static inline quat4 QuatFromVec(const vec4 &q) {
  return quat4{.x = q.x, .y = q.y, .z = q.z, .w = q.w};
}

static std::string VecString(const vec3 &v) {
  return StringPrintf(
      "(" ARED("%.4f") "," AGREEN("%.4f") "," ABLUE("%.4f") ")",
      v.x, v.y, v.z);
}

static std::string FrameString(const frame3 &f) {
  return StringPrintf(
      "frame3{.x = vec3(%.17g, %.17g, %.17g),\n"
      "       .y = vec3(%.17g, %.17g, %.17g),\n"
      "       .z = vec3(%.17g, %.17g, %.17g),\n"
      "       .o = vec3(%.17g, %.17g, %.17g)}",
      f.x.x, f.x.y, f.x.z,
      f.y.x, f.y.y, f.y.z,
      f.z.x, f.z.y, f.z.z,
      f.o.x, f.o.y, f.o.z);
}

std::string FormatNum(uint64_t n) {
  if (n > 1'000'000) {
    double m = n / 1'000'000.0;
    if (m >= 1'000'000.0) {
      return StringPrintf("%.1fT", m / 1'000'000.0);
    } else if (m >= 1000.0) {
      return StringPrintf("%.1fB", m / 1000.0);
    } else if (m >= 100.0) {
      return StringPrintf("%dM", (int)std::round(m));
    } else if (m > 10.0) {
      return StringPrintf("%.1fM", m);
    } else {
      // TODO: Integer division. color decimal place and suffix.
      return StringPrintf("%.2fM", m);
    }
  } else {
    return Util::UnsignedWithCommas(n);
  }
}

// For an oriented edge from v0 to v1,
double SignedDistanceToEdge(const vec2 &v0, const vec2 &v1,
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

double DistanceToEdge(const vec2 &v0, const vec2 &v1,
                      const vec2 &p) {
  vec2 edge = v1 - v0;
  vec2 p_edge = p - v0;
  double cx = yocto::cross(edge, p_edge);
  double dist = std::abs(cx / yocto::length(edge));
  double d0 = yocto::length(p_edge);
  double d1 = yocto::length(p - v1);

  return std::min(std::min(d0, d1), dist);
}

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
  const Faces *faces = nullptr;
};

// Rotate the polyhedron. They share the same faces pointer.
Polyhedron Rotate(const Polyhedron &p, const frame3 &frame) {
  Polyhedron ret = p;
  for (vec3 &v : ret.vertices) {
    v = yocto::transform_point(frame, v);
  }
  return ret;
}

// A polyhedron projected to 2D.
struct Mesh2D {
  std::vector<vec2> vertices;
  const Faces *faces = nullptr;
};

// Create the shadow of the polyhedron on the x-y plane.
static Mesh2D Shadow(const Polyhedron &p) {
  Mesh2D mesh;
  mesh.vertices.resize(p.vertices.size());
  for (int i = 0; i < p.vertices.size(); i++) {
    const vec3 &v = p.vertices[i];
    mesh.vertices[i] = vec2{.x = v.x, .y = v.y};
  }
  mesh.faces = p.faces;
  return mesh;
}


static double DistanceToHull(
    const Mesh2D &mesh, const std::vector<int> &hull,
    const vec2 &pt) {

  std::optional<double> best_dist;
  for (int i = 0; i < hull.size(); i++) {
    const vec2 &v0 = mesh.vertices[hull[i]];
    const vec2 &v1 = mesh.vertices[hull[(i + 1) % hull.size()]];

    double dist = DistanceToEdge(v0, v1, pt);
    if (!best_dist.has_value() || dist < best_dist.value()) {
      best_dist = {dist};
    }
  }
  CHECK(best_dist.has_value());
  return best_dist.value();
}

// Returns the convex hull (a single polygon) as indices into the
// vertex list (e.g. Mesh2D::vertices). This is the intuitive "gift
// wrapping" algorithm; much faster approaches exist, but we have a
// small number of vertices and this is in the outer loop.
static std::vector<int> ConvexHull(const std::vector<vec2> &vertices) {
  CHECK(vertices.size() > 2);

  // Find the starting point. This must be a point on
  // the convex hull. The leftmost bottommost point is
  // one.
  const int start = [&]() {
      int besti = 0;
      for (int i = 1; i < vertices.size(); i++) {
        if ((vertices[i].y < vertices[besti].y) ||
            (vertices[i].y == vertices[besti].y &&
             vertices[i].x < vertices[besti].x)) {
          besti = i;
        }
      }
      return besti;
    }();

  std::vector<int> hull;
  int cur = start;
  do {
    hull.push_back(cur);

    // We consider every other point, finding the one with
    // the smallest angle from the current point.
    int next = (cur + 1) % vertices.size();
    for (int i = 0; i < vertices.size(); i++) {
      if (i != cur && i != next) {
        const vec2 &vcur = vertices[cur];
        const vec2 &vnext = vertices[next];
        const vec2 &vi = vertices[i];

        // Compare against the current candidate, using cross
        // product to find the "leftmost" one.
        double angle = yocto::cross(vnext - vcur, vi - vcur);
        if (angle < 0.0) {
          next = i;
        }
      }
    }

    cur = next;

  } while (cur != start);

  return hull;
}


// Faces of a polyhedron must be planar. This computes the
// total error across all faces. If it is far from zero,
// something is wrong, but exact zero is not expected due
// to floating point imprecision.
static double PlanarityError(const Polyhedron &p) {
  double error = 0.0;
  for (const std::vector<int> &face : p.faces->v) {
    // Only need to check for quads and larger.
    if (face.size() > 3) {
      // The first three vertices define a plane.
      vec3 v0 = p.vertices[face[0]];
      vec3 v1 = p.vertices[face[1]];
      vec3 v2 = p.vertices[face[2]];

      vec3 normal = yocto::normalize(yocto::cross(v1 - v0, v2 - v0));

      // Check error against this plane.
      for (int i = 3; i < face.size(); i++) {
        vec3 v = p.vertices[face[i]];
        double err = std::abs(yocto::dot(v - v0, normal));
        error += err;
      }
    }
  }
  return error;
}

[[maybe_unused]]
static Polyhedron Dodecahedron() {
  constexpr bool VERBOSE = false;

  // double phi = (1.0 + sqrt(5.0)) / 2.0;
  constexpr double phi = std::numbers::phi;

  // The vertices have a nice combinatorial form.
  std::vector<vec3> vertices;

  // It's a beauty: The unit cube is in here.
  // The first eight vertices (0b000 - 0b111)
  // will be the corners of the cube, where a zero bit means
  // a coordinate of -1, and a one bit +1. The coordinates
  // are in xyz order.
  for (int i = 0b000; i < 0b1000; i++) {
    vertices.emplace_back(vec3{
        (i & 0b100) ? 1.0 : -1.0,
        (i & 0b010) ? 1.0 : -1.0,
        (i & 0b001) ? 1.0 : -1.0,
      });
  }

  for (bool j : {false, true}) {
    for (bool k : {false, true}) {
      double b = j ? phi : -phi;
      double c = k ? 1.0 / phi : -1.0 / phi;
      vertices.emplace_back(vec3{
          .x = 0.0, .y = b, .z = c});
      vertices.emplace_back(vec3{
          .x = c, .y = 0.0, .z = b});
      vertices.emplace_back(vec3{
          .x = b, .y = c, .z = 0.0});
    }
  }

  CHECK(vertices.size() == 20);

  // Rather than hard code faces, we find them from the
  // vertices. Every vertex has exactly three edges,
  // and they are to the three closest (other) vertices.

  std::vector<std::vector<int>> neighbors(20);
  for (int i = 0; i < vertices.size(); i++) {
    std::vector<std::pair<int, double>> others;
    others.reserve(19);
    for (int o = 0; o < vertices.size(); o++) {
      if (o != i) {
        others.emplace_back(o, distance(vertices[i], vertices[o]));
      }
    }
    std::sort(others.begin(), others.end(),
              [](const auto &a, const auto &b) {
                if (a.second == b.second) return a.first < b.first;
                return a.second < b.second;
              });
    others.resize(3);
    for (const auto &[idx, dist_] : others) {
      // printf("src %d, n %d, dist %.11g\n", i, idx, dist_);
      neighbors[i].push_back(idx);
    }
  }

  if (VERBOSE) {
    for (int i = 0; i < vertices.size(); i++) {
      const vec3 &v = vertices[i];
      printf("v " AWHITE("%d")
             ". (" ARED("%.3f") ", " AGREEN("%.3f") ", " ABLUE("%.3f")
             ") neighbors:", i, v.x, v.y, v.z);
      for (int n : neighbors[i]) {
        printf(" %d", n);
      }
      printf("\n");
    }
  }

  // Return the common neighbor. Aborts if there is no such
  // neighbor.
  auto CommonNeighbor = [&neighbors](int a, int b) {
      for (int aa : neighbors[a]) {
        for (int bb : neighbors[b]) {
          if (aa == bb) return aa;
        }
      }
      LOG(FATAL) << "Vertices " << a << " and " << b << " do not "
        "have a common neighbor.";
      return -1;
    };

  // Get the single neighbor of a that lies on the plane a,b,c (and
  // is not one of the arguments). Aborts if there is no such neighbor.
  auto CoplanarNeighbor = [&vertices, &neighbors](int a, int b, int c) {
      const vec3 &v0 = vertices[a];
      const vec3 &v1 = vertices[b];
      const vec3 &v3 = vertices[c];

      const vec3 normal = yocto::normalize(yocto::cross(v1 - v0, v3 - v0));

      for (int o : neighbors[a]) {
        if (o == b || o == c) continue;

        const vec3 &v = vertices[o];
        double err = std::abs(yocto::dot(v - v0, normal));
        // The other points won't even be close.
        if (err < 0.00001) {
          return o;
        }
      }

      LOG(FATAL) << "Vertices " << a << ", " << b << ", " << c <<
        " do not have a coplanar neighbor (for a).\n";
      return -1;
    };

  Faces *faces = new Faces;

  // Each face corresponds to an edge on the cube.
  for (int a = 0b000; a < 0b1000; a++) {
    for (int b = 0b000; b < 0b1000; b++) {
      // When the Hamming distance is exactly 1, this is an edge
      // of the cube. Consider only the case where a < b though.
      if (a < b && std::popcount<uint8_t>(a ^ b) == 1) {

        //       tip
        //       ,'.
        //    a,'   `.b
        //     \     /
        //      \___/
        //     o1   o2

        // These two points will share a neighbor, which is the
        // tip of the pentagon illustrated above.
        int tip = CommonNeighbor(a, b);

        int o1 = CoplanarNeighbor(a, b, tip);
        int o2 = CoplanarNeighbor(b, a, tip);

        faces->v.push_back(std::vector<int>{tip, b, o2, o1, a});
      }
    }
  }

  return Polyhedron{.vertices = std::move(vertices), .faces = faces};
}

[[maybe_unused]]
static Polyhedron SnubCube() {
  static constexpr int VERBOSE = 1;

  const double tribonacci =
    (1.0 + std::cbrt(19.0 + 3.0 * std::sqrt(33.0)) +
     std::cbrt(19.0 - 3.0 * std::sqrt(33.0))) / 3.0;

  const double a = 1.0;
  const double b = 1.0 / tribonacci;
  const double c = tribonacci;

  std::vector<vec3> vertices;

  // All even permutations with an even number of plus signs.
  //    (odd number of negative signs)
  // (a, b, c) - even
  // (b, c, a) - even
  // (c, a, b) - even

  // 1 = negative, 0 = positive
  for (const uint8_t s : {0b100, 0b010, 0b001, 0b111}) {
    vec3 signs{
      .x = (s & 0b100) ? -1.0 : 1.0,
      .y = (s & 0b010) ? -1.0 : 1.0,
      .z = (s & 0b001) ? -1.0 : 1.0,
    };

    vertices.emplace_back(vec3(a, b, c) * signs);
    vertices.emplace_back(vec3(b, c, a) * signs);
    vertices.emplace_back(vec3(c, a, b) * signs);
  }

  // And all odd permutations with an odd number of plus signs.
  //    (even number of negative signs).

  // (a, c, b) - odd
  // (b, a, c) - odd
  // (c, b, a) - odd
  // 1 = negative, 0 = positive
  for (const uint8_t s : {0b011, 0b110, 0b101, 0b000}) {
    vec3 signs{
      .x = (s & 0b100) ? -1.0 : 1.0,
      .y = (s & 0b010) ? -1.0 : 1.0,
      .z = (s & 0b001) ? -1.0 : 1.0,
    };

    vertices.emplace_back(vec3(a, c, b) * signs);
    vertices.emplace_back(vec3(b, a, c) * signs);
    vertices.emplace_back(vec3(c, b, a) * signs);
  }

  // Idea: Generate vertices.
  // Take all planes where all of the other vertices
  // are on one side. (Basically, the 3D convex hull.)
  // We can compute this pretty easily and it should
  // work for all convex polyhedra!
  // How do we order the vertices on a face, though?

  // All faces (as a set of vertices) we've already found. The
  // vertices in the face have not yet been ordered; they appear in
  // ascending sorted order.
  std::unordered_set<std::vector<int>, Hashing<std::vector<int>>>
    all_faces;

  // Given a plane defined by distinct points (v0, v1, v2), classify
  // all of the other points as either above, below, or on the plane.
  // If there are nonzero points both above and below, then this is
  // not a face; return nullopt. Otherwise, return the indices of the
  // vertices on the face, which will include at least i, j, and k.
  // The vertices are returned in sorted order (by index), which may
  // not be a proper winding for the polygon.
  auto GetFace = [&vertices](int i, int j, int k) ->
    std::optional<std::vector<int>> {
    // Three vertices define a plane.
    const vec3 &v0 = vertices[i];
    const vec3 &v1 = vertices[j];
    const vec3 &v2 = vertices[k];

    // Classify every point depending on what side it's on (or
    // whether it's on the plane). We don't need to worry about
    // ambiguity from numerical error here, as for the polyhedra
    // we consider, the points are either exactly on the plane or
    // comfortably far from it.

    const vec3 normal =
      yocto::normalize(yocto::cross(v1 - v0, v2 - v0));

    if (VERBOSE > 1) {
      printf("Try %s;%s;%s\n   Normal: %s\n",
             VecString(v0).c_str(),
             VecString(v1).c_str(),
             VecString(v2).c_str(),
             VecString(normal).c_str());
    }

    std::vector<int> coplanar;

    bool above = false, below = false;

    // Now for every other vertex...
    for (int o = 0; o < vertices.size(); o++) {
      if (o != i && o != j && o != k) {
        const vec3 &v = vertices[o];
        double dot = yocto::dot(v - v0, normal);
        if (dot < -0.00001) {
          if (above) return std::nullopt;
          below = true;
        } else if (dot > 0.00001) {
          if (below) return std::nullopt;
          above = true;
        } else {
          // On plane.
          coplanar.push_back(o);
        }
      }
    }

    CHECK(!(below && above));
    CHECK(below || above) << "This would only happen if we had "
        "a degenerate, volumeless polyhedron, which we do not.";

    coplanar.push_back(i);
    coplanar.push_back(j);
    coplanar.push_back(k);
    std::sort(coplanar.begin(), coplanar.end());
    return coplanar;
  };

  printf("There are %d vertices.\n", (int)vertices.size());

  // wlog i > j > k.
  for (int i = 0; i < vertices.size(); i++) {
    for (int j = 0; j < i; j++) {
      for (int k = 0; k < j; k++) {
        if (std::optional<std::vector<int>> fo =
            GetFace(i, j, k)) {
          all_faces.insert(std::move(fo.value()));
        }
      }
    }
  }

  printf("There are %d distinct faces.\n", (int)all_faces.size());

  // TODO: Produce the right winding order. You can sort by angle
  // from the centroid.
  Faces *faces = new Faces;
  // Make it deterministic.
  std::vector<std::vector<int>> sfaces = SetToSortedVec(all_faces);

  for (const std::vector<int> &vec : sfaces) {
    CHECK(vec.size() >= 3);
    const vec3 &v0 = vertices[vec[0]];
    const vec3 &v1 = vertices[vec[1]];
    const vec3 &v2 = vertices[vec[2]];

    /*
    const vec3 normal =
      yocto::normalize(yocto::cross(v1 - v0, v2 - v0));
    */

    // But we'll need to express the vertices on the face in terms of
    // an orthonormal basis derived from the face's plane. This means
    // computing two perpendicular vectors on the face. We'll use one
    // of the edges (normalized) and then compute the other to lie
    // in the same plane.

    vec3 a = yocto::normalize(v1 - v0);
    vec3 b = yocto::normalize(v2 - v0);
    const vec3 u = yocto::orthonormalize(a, b);
    const vec3 v = b;

    // We'll compute the angle around the centroid.
    vec3 centroid{0.0, 0.0, 0.0};
    for (int index : vec) {
      centroid += vertices[index];
    }
    centroid /= vec.size();

    std::vector<std::pair<int, double>> iangle;
    for (int i : vec) {
      const vec3 &dir = vertices[i] - centroid;
      double angle = std::atan2(yocto::dot(dir, v), yocto::dot(dir, u));
      iangle.emplace_back(i, angle);
    }

    // Now order them according to the angle.
    std::sort(iangle.begin(), iangle.end(),
              [](const auto &a, const auto &b) {
                return a.second < b.second;
              });

    // And output to the face.
    std::vector<int> face;
    face.reserve(iangle.size());
    for (const auto &[i, angle_] : iangle) {
      face.push_back(i);
    }

    faces->v.push_back(std::move(face));
  }

  return Polyhedron{.vertices = std::move(vertices), .faces = faces};
}

[[maybe_unused]]
static Polyhedron Cube() {
  //                  +y
  //      a------b     | +z
  //     /|     /|     |/
  //    / |    / |     0--- +x
  //   d------c  |
  //   |  |   |  |
  //   |  e---|--f
  //   | /    | /
  //   |/     |/
  //   h------g

  std::vector<vec3> vertices;
  auto AddVertex = [&vertices](double x, double y, double z) {
      int idx = (int)vertices.size();
      vertices.emplace_back(vec3{.x = x, .y = y, .z = z});
      return idx;
    };
  int a = AddVertex(-1, +1, +1);
  int b = AddVertex(+1, +1, +1);
  int c = AddVertex(+1, +1, -1);
  int d = AddVertex(-1, +1, -1);

  int e = AddVertex(-1, -1, +1);
  int f = AddVertex(+1, -1, +1);
  int g = AddVertex(+1, -1, -1);
  int h = AddVertex(-1, -1, -1);

  Faces *faces = new Faces;
  faces->v.reserve(6);

  // top
  faces->v.push_back({a, b, c, d});
  // bottom
  faces->v.push_back({e, f, g, h});
  // left
  faces->v.push_back({a, e, h, d});
  // right
  faces->v.push_back({b, f, g, c});
  // front
  faces->v.push_back({d, c, g, h});
  // back
  faces->v.push_back({a, b, f, e});


  return Polyhedron{.vertices = std::move(vertices), .faces = faces};
}

static quat4 RandomQuaternion(ArcFour *rc) {
  const auto &[x, y, z, w] = RandomUnit4D(rc);
  return quat4{.x = x, .y = y, .z = z, .w = w};
}

static vec2 Project(const vec3 &point, const mat4 &proj) {
  vec4 pp = proj * vec4{.x = point.x, .y = point.y, .z = point.z, .w = 1.0};
  return vec2{.x = pp.x / pp.w, .y = pp.y / pp.w};
}

static void Render(const Polyhedron &p, uint32_t color, ImageRGBA *img) {
  // Function to project a 3D point to 2D using a perspective projection

  const double scale = std::min(img->Width(), img->Height()) * 0.75;

  // Perspective projection matrix (example values)
  // double aspect = (double)1920.0 / 1080.0;
  constexpr double aspect = 1.0;
  mat4 proj = yocto::perspective_mat(
      yocto::radians(60.0), aspect, 0.1, 100.0);

  frame3 camera_frame = yocto::lookat_frame<double>(
      {0, 0, 5}, {0, 0, 0}, {0, 1, 0});
  mat4 view_matrix = yocto::frame_to_mat(camera_frame);
  mat4 model_view_projection = proj * view_matrix;

  for (const std::vector<int> &face : p.faces->v) {
    for (int i = 0; i < (int)face.size(); i++) {
      vec3 v0 = p.vertices[face[i]];
      vec3 v1 = p.vertices[face[(i + 1) % face.size()]];

      vec2 p0 = Project(v0, model_view_projection);
      vec2 p1 = Project(v1, model_view_projection);

      float x0 = (p0.x * scale + img->Width() * 0.5);
      float y0 = (p0.y * scale + img->Height() * 0.5);
      float x1 = (p1.x * scale + img->Width() * 0.5);
      float y1 = (p1.y * scale + img->Height() * 0.5);
      img->BlendThickLine32(x0, y0, x1, y1, 4.0f, color & 0xFFFFFF88);
    }
  }
}

static std::vector<uint32_t> COLORS = {
  0xFF0000FF,
  0xFFFF00FF,
  0x00FF00FF,
  0x00FFFFFF,
  0x0000FFFF,
  0xFF00FFFF,
  0x770000FF,
  0x777700FF,
  0x007700FF,
  0x007777FF,
  0x000077FF,
  0x770077FF,
  0xFF7777FF,
  0xFFFF77FF,
  0x77FF77FF,
  0x77FFFFFF,
  0x7777FFFF,
  0xFF77FFFF,
  0x330000FF,
  0x333300FF,
  0x003300FF,
  0x003333FF,
  0x000033FF,
  0x330033FF,
  0x77333377,
  0x77773377,
  0x33773377,
  0x33777777,
  0x33337777,
  0x77337777,
  0xFFAAAAFF,
  0xFFFFAAFF,
  0xAAFFAAFF,
  0xAAFFFFFF,
  0xAAAAFFFF,
  0xFFAAFFFF,
  0xFF3333FF,
  0xFFFF33FF,
  0x33FF33FF,
  0x33FFFFFF,
  0x3333FFFF,
  0xFF33FFFF,
};

// Point-in-polygon test using the winding number algorithm
bool PointInPolygon(const vec2 &point,
                    const std::vector<vec2> &vertices,
                    const std::vector<int> &polygon) {
  int winding_number = 0;
  for (int i = 0; i < polygon.size(); i++) {
    const vec2 &p0 = vertices[polygon[i]];
    const vec2 &p1 = vertices[polygon[(i + 1) % polygon.size()]];

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

constexpr double MESH_SCALE = 0.20;

static void RenderMesh(const Mesh2D &mesh,
                       ImageRGBA *img) {
  const int w = img->Width();
  const int h = img->Height();
  // XXX compute this from the polyhedron's diameter
  const double scale = std::min(w, h) * MESH_SCALE;

  CHECK(mesh.faces->v.size() < COLORS.size()) << mesh.faces->v.size()
                                              << " but have "
                                              << COLORS.size();

  auto ToWorld = [w, h, scale](int sx, int sy) -> vec2 {
      // Center of screen should be 0,0.
      double cy = sy - h / 2.0;
      double cx = sx - w / 2.0;
      return vec2{.x = cx / scale, .y = cy / scale};
    };
  auto ToScreen = [w, h, scale](const vec2 &pt) -> std::pair<int, int> {
    double cx = pt.x * scale;
    double cy = pt.y * scale;
    return std::make_pair(cx + w / 2.0, cy + h / 2.0);
  };

  // Draw filled polygons first.
  for (int sy = 0; sy < h; sy++) {
    for (int sx = 0; sx < w; sx++) {
      vec2 pt = ToWorld(sx, sy);
      for (int i = 0; i < mesh.faces->v.size(); i++) {
        if (PointInPolygon(pt, mesh.vertices, mesh.faces->v[i])) {
          img->BlendPixel32(sx, sy, COLORS[i] & 0xFFFFFF22);
        }
      }
    }
  }

  // Draw lines on top.
  for (const std::vector<int> &face : mesh.faces->v) {
    for (int i = 0; i < face.size(); i++) {
      const vec2 &a = mesh.vertices[face[i]];
      const vec2 &b = mesh.vertices[face[(i + 1) % face.size()]];

      const auto &[ax, ay] = ToScreen(a);
      const auto &[bx, by] = ToScreen(b);
      img->BlendThickLine32(ax, ay, bx, by, 3.0, 0xFFFFFF99);
    }
  }
}

static void RenderHull(const Mesh2D &mesh,
                       const std::vector<int> &hull,
                       ImageRGBA *img) {
  const int w = img->Width();
  const int h = img->Height();
  // XXX compute this from the polyhedron's diameter
  const double scale = std::min(w, h) * MESH_SCALE;

  auto ToScreen = [w, h, scale](const vec2 &pt) -> std::pair<int, int> {
    double cx = pt.x * scale;
    double cy = pt.y * scale;
    return std::make_pair(cx + w / 2.0, cy + h / 2.0);
  };

  for (int i = 0; i < hull.size(); i++) {
    const vec2 &v0 = mesh.vertices[hull[i]];
    const vec2 &v1 = mesh.vertices[hull[(i + 1) % hull.size()]];

    const auto &[x0, y0] = ToScreen(v0);
    const auto &[x1, y1] = ToScreen(v1);

    img->BlendThickLine32(x0, y0, x1, y1, 2.0, 0x00FF00AA);
  }

  for (int i = 0; i < hull.size(); i++) {
    const vec2 &v0 = mesh.vertices[hull[i]];
    const auto &[x, y] = ToScreen(v0);
    img->BlendText32(x - 12, y - 12, 0xFFFF00FF,
                     StringPrintf("%d", i));
  }

}

static bool InMesh(const Mesh2D &mesh, const vec2 &pt) {
  for (const std::vector<int> &face : mesh.faces->v)
    if (PointInPolygon(pt, mesh.vertices, face))
      return true;

  return false;
}

static bool InHull(const Mesh2D &mesh, const std::vector<int> &hull,
                   const vec2 &pt) {
  return PointInPolygon(pt, mesh.vertices, hull);
}

[[maybe_unused]]
static void AnimateMesh() {
  ArcFour rc("animate");
  // const Polyhedron poly = Cube();
  // const Polyhedron poly = Dodecahedron();
  const Polyhedron poly = SnubCube();
  quat4 initial_rot = RandomQuaternion(&rc);

  constexpr int SIZE = 1080;
  constexpr int FRAMES = 10 * 60;
  // constexpr int FRAMES = 10;

  MovRecorder rec("animate.mov", SIZE, SIZE);

  StatusBar status(2);
  Periodically status_per(1.0);
  for (int i = 0; i < FRAMES; i++) {
    if (status_per.ShouldRun()) {
      status.Progressf(i, FRAMES, "rotate");
    }

    double t = i / (double)FRAMES;
    double angle = t * 2.0 * std::numbers::pi;

    // rotation quat actually returns vec4; isomorphic to quat4.
    quat4 frame_rot =
      QuatFromVec(yocto::rotation_quat<double>({0.0, 1.0, 0.0}, angle));

    quat4 final_rot = normalize(initial_rot * frame_rot);
    Polyhedron rpoly = Rotate(poly, yocto::rotation_frame(final_rot));

    ImageRGBA img(SIZE, SIZE);
    img.Clear32(0x000000FF);
    Mesh2D mesh = Shadow(rpoly);
    RenderMesh(mesh, &img);
    rec.AddFrame(std::move(img));
  }
}

[[maybe_unused]]
static void Visualize() {
  // ArcFour rc(StringPrintf("seed.%lld", time(nullptr)));
  ArcFour rc("fixed-seed");

  // const Polyhedron poly = Cube();
  // const Polyhedron poly = Dodecahedron();
  const Polyhedron poly = SnubCube();
  CHECK(PlanarityError(poly) < 1.0e-10);

  {
    ImageRGBA img(1920, 1080);
    img.Clear32(0x000000FF);
    for (int i = 0; i < 5; i++) {
      frame3 frame = yocto::rotation_frame(RandomQuaternion(&rc));
      Polyhedron rpoly = Rotate(poly, frame);

      CHECK(PlanarityError(rpoly) < 1.0e10);
      Render(rpoly, COLORS[i], &img);
    }

    img.Save("wireframe.png");
  }

  {
    ImageRGBA img(1920, 1080);
    img.Clear32(0x000000FF);

    quat4 q = RandomQuaternion(&rc);
    frame3 frame = yocto::rotation_frame(q);

    Polyhedron rpoly = Rotate(poly, frame);

    Mesh2D mesh = Shadow(rpoly);
    RenderMesh(mesh, &img);

    std::vector<int> hull = ConvexHull(mesh.vertices);
    RenderHull(mesh, hull, &img);

    img.Save("shadow.png");
    printf("Wrote shadow.png\n");
  }
}

static void Solve(const Polyhedron &polyhedron) {
  // ArcFour rc(StringPrintf("solve.%lld", time(nullptr)));

  static constexpr int HISTO_LINES = 32;

  std::mutex m;
  bool should_die = false;
  Timer run_timer;
  StatusBar status(3 + HISTO_LINES);
  Periodically status_per(1.0);
  Periodically image_per(10.0);
  double best_error = 1.0e42;
  AutoHisto error_histo(10000);
  constexpr int NUM_THREADS = 4;

  double prep_time = 0.0, opt_time = 0.0;

  ParallelFan(
      NUM_THREADS,
      [&](int thread_idx) {
        ArcFour rc(StringPrintf("solve.%d.%lld", thread_idx,
                                time(nullptr)));
        for (;;) {
          {
            MutexLock ml(&m);
            if (should_die) return;
          }

          Timer prep_timer;
          quat4 outer_rot = RandomQuaternion(&rc);
          const frame3 outer_frame = yocto::rotation_frame(outer_rot);
          Polyhedron outer = Rotate(polyhedron, outer_frame);
          Mesh2D souter = Shadow(outer);

          const std::vector<int> shadow_hull = ConvexHull(souter.vertices);

          // Starting orientation/position.
          const quat4 inner_rot = RandomQuaternion(&rc);

          static constexpr int D = 6;
          auto InnerFrame = [&inner_rot](const std::array<double, D> &args) {
              const auto &[di, dj, dk, dl, dx, dy] = args;
              quat4 tweaked_rot = normalize(quat4{
                  .x = inner_rot.x + di,
                  .y = inner_rot.y + dj,
                  .z = inner_rot.z + dk,
                  .w = inner_rot.w + dl,
                });
              frame3 rotate = yocto::rotation_frame(tweaked_rot);
              frame3 translate = yocto::translation_frame(
                  vec3{.x = dx, .y = dy, .z = 0.0});
              return rotate * translate;
            };

          auto WriteImage = [&](const std::string &filename,
                                const std::array<double, D> &args) {
              // Show:
              ImageRGBA img(3840, 2160);
              img.Clear32(0x000000FF);

              RenderMesh(souter, &img);
              // Darken background.
              for (int y = 0; y < img.Height(); y++) {
                for (int x = 0; x < img.Width(); x++) {
                  img.BlendPixel32(x, y, 0x550000AA);
                }
              }

              auto inner_frame = InnerFrame(args);
              Polyhedron inner = Rotate(polyhedron, inner_frame);
              Mesh2D sinner = Shadow(inner);
              RenderMesh(sinner, &img);

              img.Save(filename);

              status.Printf("Wrote " AGREEN("%s") "\n", filename.c_str());
            };

          auto Parameters = [&](const std::array<double, D> &args,
                                double error) {
              auto inner_frame = InnerFrame(args);
              std::string contents =
                StringPrintf("Error: %.17g\n", error);

              contents += "Outer frame:\n";
              contents += FrameString(outer_frame);
              contents += "\nInner frame:\n";
              contents += FrameString(inner_frame);
              StringAppendF(&contents,
                            "\nTook %lld iters, %lld attempts, %.3f seconds\n",
                            iters.Read(), attempts.Read(), run_timer.Seconds());
              return contents;
            };


          std::function<double(const std::array<double, D> &)> Loss =
            [&polyhedron, &souter, &shadow_hull, &InnerFrame](
                const std::array<double, D> &args) {
              attempts++;
              frame3 frame = InnerFrame(args);
              Polyhedron inner = Rotate(polyhedron, frame);
              Mesh2D sinner = Shadow(inner);

              // Does every vertex in inner fall inside the outer shadow?
              double error = 0.0;
              for (const vec2 &iv : sinner.vertices) {
                if (!InHull(souter, shadow_hull, iv)) {
                  // PERF we should get the distance to the convex hull,
                  // but distance from the origin should at least have
                  // the right slope.
                  // error += length(iv);
                  error += DistanceToHull(souter, shadow_hull, iv);
                }
              }

              return error;
            };

          const std::array<double, D> lb =
            {-0.15, -0.15, -0.15, -0.15, -0.25, -0.25};
          const std::array<double, D> ub =
            {+0.15, +0.15, +0.15, +0.15, +0.25, +0.25};
          const double prep_sec = prep_timer.Seconds();

          Timer opt_timer;
          const auto &[args, error] =
            Opt::Minimize<D>(Loss, lb, ub, 1000, 2);
          const double opt_sec = opt_timer.Seconds();

          if (error == 0.0) {
            MutexLock ml(&m);
            should_die = true;

            status.Printf("Solved! %lld iters, %lld attempts, in %s\n",
                          iters.Read(),
                          attempts.Read(),
                          ANSI::Time(run_timer.Seconds()).c_str());

            WriteImage("solved.png", args);

            std::string contents = Parameters(args, error);
            StringAppendF(&contents,
                          "\n%s\n",
                          error_histo.SimpleAsciiString(50).c_str());

            Util::WriteFile("solution.txt", contents);
            status.Printf("Wrote " AGREEN("solution.txt") "\n");

            return;
          }

          {
            MutexLock ml(&m);
            prep_time += prep_sec;
            opt_time += opt_sec;
            error_histo.Observe(log(error));
            if (error < best_error) {
              best_error = error;
              if (image_per.ShouldRun()) {
                std::string file_base =
                  StringPrintf("best.%lld", iters.Read());
                WriteImage(file_base + ".png", args);
                Util::WriteFile(file_base + ".txt", Parameters(args, error));
              }
            }

            status_per.RunIf([&]() {
                double total_time = prep_time + opt_time;

                int64_t it = iters.Read();
                double ips = it / total_time;

                status.Statusf(
                    "%s\n"
                    "%s " ABLUE("prep") " %s " APURPLE("opt")
                    " (" ABLUE("%.3f%%") " / " APURPLE("%.3f%%") ") "
                    "[" AWHITE("%.3f") "/s]\n"
                    "%s iters, %s attempts; best: %.11g",
                    error_histo.SimpleANSI(HISTO_LINES).c_str(),
                    ANSI::Time(prep_time).c_str(),
                    ANSI::Time(opt_time).c_str(),
                    (100.0 * prep_time) / total_time,
                    (100.0 * opt_time) / total_time,
                    ips,
                    FormatNum(it).c_str(),
                    FormatNum(attempts.Read()).c_str(),
                    best_error);
              });
          }

          iters++;
        }
      });
}


int main(int argc, char **argv) {
  ANSI::Init();
  printf("\n");

  // (void)SnubCube();
  Visualize();
  // AnimateMesh();

  printf("\n");
  // Solve(Cube());
  // Solve(Dodecahedron());
  Solve(SnubCube());

  printf("OK\n");
  return 0;
}
