
#include "hull-3d.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <string>
#include <vector>

#include "ansi.h"
#include "arcfour.h"
#include "base/logging.h"
#include "base/print.h"
#include "randutil.h"
#include "yocto-math.h"

using namespace std;
using vec3 = Hull3D::vec3;

static std::string VecString(const vec3 &v) {
  return std::format(
      "(" ARED("{:.4f}") "," AGREEN("{:.4f}") "," ABLUE("{:.4f}") ")",
      v.x, v.y, v.z);
}

static void TestTetrahedron() {
  vector<vec3> points = {
    {0.0, 0.0, 0.0},
    {1.0, 0.0, 0.0},
    {0.0, 1.0, 0.0},
    {0.0, 0.0, 1.0},
    // Inside point
    {0.25, 0.25, 0.25},
  };

  vector<int> indices = Hull3D::HullPoints(points);
  CHECK(indices.size() == 4);

  vector<vec3> reduced = Hull3D::ReduceToHull(points);
  CHECK(reduced.size() == 4);

  auto faces = Hull3D::HullFaces(points);
  CHECK(faces.size() == 4);

  for (const auto &[a, b, c] : faces) {
    CHECK(a >= 0 && a < (int)points.size());
    CHECK(b >= 0 && b < (int)points.size());
    CHECK(c >= 0 && c < (int)points.size());
  }
}

static void TestCube() {
  vector<vec3> points = {
    {0.0, 0.0, 0.0},
    {1.0, 0.0, 0.0},
    {1.0, 1.0, 0.0},
    {0.0, 1.0, 0.0},
    {0.0, 0.0, 1.0},
    {1.0, 0.0, 1.0},
    {1.0, 1.0, 1.0},
    {0.0, 1.0, 1.0},
    // Inside point
    {0.5, 0.5, 0.5},
  };

  vector<int> indices = Hull3D::HullPoints(points);
  CHECK(indices.size() == 8);

  vector<vec3> reduced = Hull3D::ReduceToHull(points);
  CHECK(reduced.size() == 8);

  auto faces = Hull3D::HullFaces(points);
  // 6 square faces triangulated into exactly 12 triangles.
  CHECK(faces.size() == 12);

  for (const auto &[a, b, c] : faces) {
    CHECK(a >= 0 && a < (int)points.size());
    CHECK(b >= 0 && b < (int)points.size());
    CHECK(c >= 0 && c < (int)points.size());
  }
}

static void TestOctahedronRegression1() {
  std::vector<vec3> vertices{
    {-1.0000, 0.0000, 0.0000},
    {0.0000, -1.0000, 0.0000},
    {0.0335, 0.0128, 0.0709},
    {0.0000, 0.0000, -1.0000},
    {0.0000, 1.0000, 0.0000},
    {0.0000, 0.0000, 1.0000},
    {1.0000, 0.0000, 0.0000},
  };

  std::vector<int> hull = Hull3D::HullPoints(vertices);
  if (hull.size() != 6) {
    Print("Hull is wrong size ({}):\n", hull.size());
    for (int i : hull) {
      Print("  #{}. {}\n", i, VecString(vertices[i]));
    }
    CHECK(hull.size() == 6);
  }
  for (int i : hull) {
    CHECK(i != 2) << "This point is definitely not in the hull!";
  }
}

static void TestOctahedron() {
  ArcFour rc("octahedron");
  static constexpr int VERBOSE = 0;

  const double sqrt6 = std::sqrt(6.0);
  for (int passes = 0; passes < 50; passes++) {
    std::vector<vec3> vertices;
    for (double s : {-1.0, 1.0}) {
      vertices.emplace_back(s, 0.0, 0.0);
      vertices.emplace_back(0.0, s, 0.0);
      vertices.emplace_back(0.0, 0.0, s);
    }

    // Now a bunch of random vertices strictly within the inscribed
    // sphere.
    for (int i = 0; i < 10000; i++) {
      vec3 v = normalize(
          vec3(RandDouble(&rc), RandDouble(&rc), RandDouble(&rc)));
      vertices.push_back((v * RandDouble(&rc) * 0.9999999) / sqrt6);
    }

    Shuffle(&rc, &vertices);

    if (VERBOSE > 1) {
      Print("Hull of:\n");
      for (int i = 0; i < vertices.size(); i++) {
        Print("  #{}: {}\n", i, VecString(vertices[i]));
      }
    }

    std::vector<int> hull = Hull3D::HullPoints(vertices);

    if (hull.size() != 6) {
      Print("Hull should be the octahedron, but got {} points:\n",
            hull.size());
      for (int i : hull) {
        Print("  #{}: vertex {}, {}\n",
              i, hull[i], VecString(vertices[hull[i]]));
      }
      LOG(FATAL) << "It's wrong";
    }

    for (int i : hull) {
      const vec3 &v = vertices[i];
      if (std::abs(v.x) == 1.0) {
        CHECK(v.y == 0.0 && v.z == 0.0);
      } else if (std::abs(v.y) == 1.0) {
        CHECK(v.x == 0.0 && v.z == 0.0);
      } else if (std::abs(v.z) == 1.0) {
        CHECK(v.x == 0.0 && v.y == 0.0);
      } else {
        LOG(FATAL) << i << " not an octahedron vertex: " << VecString(v);
      }
    }

    if (VERBOSE) Print("ok\n");
  }
}

int main(int argc, char **argv) {
  ANSI::Init();

  TestTetrahedron();
  TestCube();

  TestOctahedronRegression1();
  TestOctahedron();

  Print("OK\n");
  return 0;
}



