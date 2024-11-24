
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <functional>
#include <numbers>
#include <utility>
#include <vector>

#include "ansi.h"
#include "arcfour.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "image.h"
#include "mov-recorder.h"
#include "mov.h"
#include "opt/opt.h"
#include "periodically.h"
#include "randutil.h"
#include "status-bar.h"
#include "timer.h"
#include "auto-histo.h"
#include "atomic-util.h"

#include "yocto_matht.h"
#include "yocto_geometryt.h"

DECLARE_COUNTERS(attempts, u1_, u2_, u3_, u4_, u5_, u6_, u7_);

using vec2 = yocto::vec<double, 2>;
using vec3 = yocto::vec<double, 3>;
using vec4 = yocto::vec<double, 4>;
using mat4 = yocto::mat<double, 4>;
using quat4 = yocto::quat<double, 4>;
using frame3 = yocto::frame<double, 3>;

static inline vec4 VecFromQuat(const quat4 &q) {
  return vec4{.x = q.x, .y = q.y, .z = q.z, .w = q.w};
}

static inline quat4 QuatFromVec(const vec4 &q) {
  return quat4{.x = q.x, .y = q.y, .z = q.z, .w = q.w};
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
      vec3 v3 = p.vertices[face[2]];

      vec3 normal = yocto::normalize(yocto::cross(v1 - v0, v3 - v0));

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

static Polyhedron Dodecahedron() {
  constexpr double phi = std::numbers::phi; // double phi = (1.0 + sqrt(5.0)) / 2.0;

  // The vertices have a nice combinatorial form.
  std::vector<vec3> vertices;
  for (bool i : {false, true}) {
    for (bool j : {false, true}) {
      for (bool k : {false, true}) {
        // It's a beauty: The unit cube is in here.
        vertices.emplace_back(vec3{
            i ? 1.0 : -1.0,
            j ? 1.0 : -1.0,
            k ? 1.0 : -1.0,
          });
      }
    }
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

  // XXXX
  return Polyhedron();
}

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

static std::array<uint32_t, 7> COLORS = {
  0xFF0000FF,
  0xFFFF00FF,
  0x00FF00FF,
  0x00FFFFFF,
  0x0000FFFF,
  0xFF00FFFF,
  0x7777FFFF,
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

static void RenderMesh(const Mesh2D &mesh, ImageRGBA *img) {
  const int w = img->Width();
  const int h = img->Height();
  const double scale = std::min(w, h) * 0.25;

  CHECK(mesh.faces->v.size() < COLORS.size());

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

static bool InMesh(const Mesh2D &mesh, const vec2 &pt) {
  for (const std::vector<int> &face : mesh.faces->v)
    if (PointInPolygon(pt, mesh.vertices, face))
      return true;

  return false;
}

[[maybe_unused]]
static void AnimateMesh(ArcFour *rc) {
  const Polyhedron cube = Cube();
  quat4 initial_rot = RandomQuaternion(rc);

  constexpr int SIZE = 1080;
  constexpr int FRAMES = 10 * 60;
  // constexpr int FRAMES = 10;

  MovRecorder rec("cube.mov", SIZE, SIZE);

  StatusBar status(1);
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
    Polyhedron rcube = Rotate(cube, yocto::rotation_frame(final_rot));

    ImageRGBA img(SIZE, SIZE);
    img.Clear32(0x000000FF);
    Mesh2D mesh = Shadow(rcube);
    RenderMesh(mesh, &img);
    rec.AddFrame(std::move(img));
  }
}

[[maybe_unused]]
static void Visualize() {
  // ArcFour rc(StringPrintf("seed.%lld", time(nullptr)));
  ArcFour rc("fixed-seed");

  const Polyhedron cube = Cube();
  CHECK(PlanarityError(cube) < 1.0e-10);

  {
    ImageRGBA img(1920, 1080);
    img.Clear32(0x000000FF);
    for (int i = 0; i < 5; i++) {
      frame3 frame = yocto::rotation_frame(RandomQuaternion(&rc));
      Polyhedron rcube = Rotate(cube, frame);

      CHECK(PlanarityError(rcube) < 1.0e10);
      Render(rcube, COLORS[i], &img);
    }

    img.Save("cubes.png");
  }


  {
    ImageRGBA img(1920, 1080);
    img.Clear32(0x000000FF);

    quat4 q = RandomQuaternion(&rc);
    frame3 frame = yocto::rotation_frame(q);

    Polyhedron rcube = Rotate(cube, frame);

    Mesh2D mesh = Shadow(rcube);
    RenderMesh(mesh, &img);

    img.Save("shadow.png");
  }
}

static void Solve(const Polyhedron &polyhedron) {
  ArcFour rc(StringPrintf("solve.%lld", time(nullptr)));
  Timer run_timer;
  StatusBar status(3);
  Periodically status_per(1.0);
  double best_error = 1.0e42;
  AutoHisto error_histo(10000);

  for (int iters = 0; true; iters++) {
    quat4 outer_rot = RandomQuaternion(&rc);
    Polyhedron outer = Rotate(polyhedron, yocto::rotation_frame(outer_rot));
    Mesh2D souter = Shadow(outer);

    // Starting orientation/position.
    quat4 inner_rot = RandomQuaternion(&rc);

    static constexpr int D = 6;
    std::function<double(const std::array<double, D> &)> Loss =
      [&polyhedron, &souter, &inner_rot](const std::array<double, D> &args) {
        attempts++;
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
        Polyhedron inner = Rotate(polyhedron, rotate * translate);

        Mesh2D sinner = Shadow(inner);

        // Does every vertex in inner fall inside the outer shadow?
        double error = 0.0;
        for (const vec2 &iv : sinner.vertices) {
          if (!InMesh(souter, iv)) {
            // PERF we should get the distance to the convex hull,
            // but distance from the origin should at least have
            // the right slope.
            error += length(iv);
          }
        }

        return error;
      };

    const std::array<double, D> lb = {-0.1, -0.1, -0.1, -0.1, -0.1, -0.1};
    const std::array<double, D> ub = {+0.1, +0.1, +0.1, +0.1, +0.1, +0.1};
    const auto &[args, error] =
      Opt::Minimize<D>(Loss, lb, ub, 1000);
    error_histo.Observe(error);
    best_error = std::min(best_error, error);

    if (error == 0.0) {
      printf("Solved! %d iters, %lld attempts, in %s\n",
             iters,
             attempts.Read(),
             ANSI::Time(run_timer.Seconds()).c_str());
      return;
    }

    if (status_per.ShouldRun()) {
      status.Statusf("%s\n%d iters, best: %.11g",
                     error_histo.SimpleHorizANSI(12).c_str(),
                     iters, best_error);
    }
  }
}


int main(int argc, char **argv) {
  ANSI::Init();

  printf("\n");
  Solve(Cube());

  printf("OK\n");
  return 0;
}
