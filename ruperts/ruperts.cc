
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <vector>

#include "ansi.h"
#include "arcfour.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "randutil.h"
#include "image.h"

#include "yocto_matht.h"
#include "yocto_geometryt.h"

using vec2 = yocto::vec<double, 2>;
using vec3 = yocto::vec<double, 3>;
using vec4 = yocto::vec<double, 4>;
using mat4 = yocto::mat<double, 4>;
using quat4 = yocto::quat<double, 4>;
using frame3 = yocto::frame<double, 3>;

struct Polyhedron {
  // A polyhedron is nominally centered around (0,0).
  // This vector contains the positions of the vertices
  // in the polyhedron. The indices of the vertices are
  // significant.
  std::vector<vec3> vertices;
  // A face is represented as the list of vertex (indices)
  // that circumscribe it. The vertices may appear in
  // clockwise our counter-clockwise order.
  std::vector<std::vector<int>> faces;
};

Polyhedron Rotate(const Polyhedron &p, const frame3 &frame) {
  Polyhedron ret = p;
  for (vec3 &v : ret.vertices) {
    v = yocto::transform_point(frame, v);
  }
  return ret;
}

// Faces of a polyhedron must be planar. This computes the
// total error across all faces. If it is far from zero,
// something is wrong, but exact zero is not expected due
// to floating point imprecision.
static double PlanarityError(const Polyhedron &p) {
  double error = 0.0;
  for (const std::vector<int> &face : p.faces) {
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

  Polyhedron cube;
  auto AddVertex = [&cube](double x, double y, double z) {
      int idx = (int)cube.vertices.size();
      cube.vertices.emplace_back(vec3{.x = x, .y = y, .z = z});
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

  // top
  cube.faces.push_back({a, b, c, d});
  // bottom
  cube.faces.push_back({e, f, g, h});
  // left
  cube.faces.push_back({a, e, h, d});
  // right
  cube.faces.push_back({b, f, g, c});
  // front
  cube.faces.push_back({d, c, g, h});
  // back
  cube.faces.push_back({a, b, f, e});

  return cube;
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

  for (const std::vector<int> &face : p.faces) {
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

static constexpr uint32_t COLORS[] = {
  0xFF0000FF,
  0xFFFF00FF,
  0x00FF00FF,
  0x00FFFFFF,
  0x0000FFFF,
  0xFF00FFFF,
};

static void Run() {
  ArcFour rc(StringPrintf("seed.%lld", time(nullptr)));
  const Polyhedron cube = Cube();
  CHECK(PlanarityError(cube) < 1.0e-10);

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

int main(int argc, char **argv) {
  ANSI::Init();

  Run();

  printf("OK\n");
  return 0;
}
