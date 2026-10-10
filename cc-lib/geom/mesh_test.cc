#include <cmath>

#include "mesh.h"
#include "util.h"

#include <cstdio>
#include <cstdlib>
#include <format>
#include <tuple>
#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "yocto-math.h"

using vec3 = yocto::vec<double, 3>;

double IsNear(double a, double b) {
  return std::abs(a - b) < 0.0000001;
}

#define CHECK_NEAR(f, g) do {                                           \
  const double fv = (f);                                                \
  const double gv = (g);                                                \
  const double e = std::abs(fv - gv);                                   \
  CHECK(e < 0.0000001) << "Expected " << #f << " and " << #g <<         \
    " to be close, but got: " <<                                        \
    std::format("{:.17g} and {:.17g}, with err {:.17g}", fv, gv, e);    \
  } while (0)

static void VolumeOfCube() {

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
  auto Vertex = [&vertices](double x, double y, double z) {
      int idx = vertices.size();
      vertices.push_back(vec3{.x = x, .y = y, .z = z});
      return idx;
    };

  const int a = Vertex(0.0, 1.0, 1.0);
  const int b = Vertex(1.0, 1.0, 1.0);
  const int c = Vertex(1.0, 1.0, 0.0);
  const int d = Vertex(0.0, 1.0, 0.0);

  const int e = Vertex(0.0, 0.0, 1.0);
  const int f = Vertex(1.0, 0.0, 1.0);
  const int g = Vertex(1.0, 0.0, 0.0);
  const int h = Vertex(0.0, 0.0, 0.0);

  std::vector<std::tuple<int, int, int>> triangles;
  // top
  triangles.emplace_back(a, b, d);
  triangles.emplace_back(b, c, d);
  // right
  triangles.emplace_back(c, b, g);
  triangles.emplace_back(b, f, g);
  // left
  triangles.emplace_back(a, d, h);
  triangles.emplace_back(h, e, a);
  // front
  triangles.emplace_back(d, c, h);
  triangles.emplace_back(h, c, g);
  // back
  triangles.emplace_back(b, a, e);
  triangles.emplace_back(e, f, b);
  // bottom
  triangles.emplace_back(h, f, e);
  triangles.emplace_back(g, f, h);

  TriangularMesh3D cube{.vertices = vertices, .triangles = triangles};

  CHECK_NEAR(MeshVolume(cube), 1.0);

  OrientMesh(&cube);

  CHECK_NEAR(MeshVolume(cube), 1.0);
}


// SaveAsSTL(..., exact = true) prints exact decimal expansions, and
// LoadSTL reads the same doubles back.
static void ExactSTL() {
  // The Septopert (2^-20 dyadic coordinates) plus some awkward doubles.
  const double s = 1.0 / 1048576.0;
  TriangularMesh3D mesh;
  mesh.vertices = {
    vec3{-940638 * s, 327651 * s, 327651 * s},
    vec3{47788 * s, 703968 * s, 47788 * s},
    vec3{-514207 * s, 4.9406564584124654e-324, -0.0},
    vec3{1.0e300, -3.0, 0.1},
  };
  mesh.triangles = {{0, 1, 2}, {0, 1, 3}};
  const std::string file = "mesh_test_exact.stl";
  SaveAsSTL(mesh, file, "exact", true, true);
  const std::string contents = Util::ReadFile(file);
  CHECK(contents.find("-0.8970623016357421875 ") != std::string::npos);
  CHECK(contents.find("0.045574188232421875 ") != std::string::npos);
  // 0.1 is not exactly one tenth as a double; its exact expansion has 55
  // fractional digits.
  CHECK(contents.find("0.1000000000000000055511151231257827021181583404541015625")
        != std::string::npos);
  TriangularMesh3D back = LoadSTL(file);
  CHECK(back.triangles.size() == 2);
  for (const auto &[a, b, c] : back.triangles) {
    for (int v : {a, b, c}) {
      const vec3 &p = back.vertices[v];
      bool found = false;
      for (const vec3 &q : mesh.vertices) {
        if (p.x == q.x && p.y == q.y && p.z == q.z &&
            std::signbit(p.z) == std::signbit(q.z)) found = true;
      }
      CHECK(found) << p.x << " " << p.y << " " << p.z;
    }
  }
  std::remove(file.c_str());
}

int main(int argc, char **argv) {
  ANSI::Init();

  VolumeOfCube();
  ExactSTL();

  printf("OK\n");
  return 0;
}
