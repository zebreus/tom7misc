
#include "polygonization.h"

#include <array>
#include <cmath>
#include <format>
#include <cstdint>
#include <numbers>
#include <string_view>
#include <variant>
#include <vector>

#include "ansi.h"
#include "arcfour.h"
#include "base/logging.h"
#include "base/print.h"
#include "image.h"
#include "randutil.h"
#include "yocto-math.h"

using vec2 = Polygonization::vec2;

static constexpr int kWidth = 1200;
static constexpr int kHeight = 1200;

static Polygonization::Shape GenerateShape(int i, ArcFour *rc) {
  Polygonization::Shape shape;
  std::vector<vec2> boundary;
  double cx = kWidth / 2.0;
  double cy = kHeight / 2.0;
  for (int j = 0; j < 30; j++) {
    double angle = j * 2.0 * std::numbers::pi / 30.0;
    double r = 400.0 + RandDouble(rc) * 50.0;
    boundary.push_back({cx + r * std::cos(angle), cy + r * std::sin(angle)});
  }
  shape.points.push_back(boundary);

  for (int j = 0; j < i; j++) {
    std::vector<vec2> hole;
    double h_angle = j * 2.0 * std::numbers::pi / i;
    double hx = cx + 200.0 * std::cos(h_angle);
    double hy = cy + 200.0 * std::sin(h_angle);
    for (int k = 0; k < 10; k++) {
      double angle = k * 2.0 * std::numbers::pi / 10.0;
      double r = 30.0 + RandDouble(rc) * 10.0;
      hole.push_back({hx + r * std::cos(angle), hy + r * std::sin(angle)});
    }
    shape.points.push_back(hole);
  }

  if (i > 5) {
    std::vector<vec2> boundary2;
    double cx2 = 150.0;
    double cy2 = 150.0;
    for (int j = 0; j < 20; j++) {
      double angle = j * 2.0 * std::numbers::pi / 20.0;
      double r = 50.0 + RandDouble(rc) * 20.0;
      boundary2.push_back({
          .x = cx2 + r * std::cos(angle),
          .y = cy2 + r * std::sin(angle),
        });
    }
    shape.points.push_back(boundary2);

    std::vector<vec2> hole2;
    for (int k = 0; k < 10; k++) {
      double angle = k * 2.0 * std::numbers::pi / 10.0;
      double r = 10.0 + RandDouble(rc) * 10.0;
      hole2.push_back({
          .x = cx2 + r * std::cos(angle),
          .y = cy2 + r * std::sin(angle),
        });
    }
    shape.points.push_back(hole2);
  }

  return shape;
}

static void DebugDrawTriangulate() {
  ArcFour rc("test");

  for (int i = 0; i < 10; i++) {
    ImageRGBA img(kWidth, kHeight);
    img.Clear32(0x000000FF);

    Polygonization::Shape shape = GenerateShape(i, &rc);

    Polygonization::TriangulateResult res =
      Polygonization::Triangulate(shape);
    if (const std::string_view *err = std::get_if<std::string_view>(&res)) {
      LOG(FATAL) << *err;
    }
    CHECK(std::holds_alternative<Polygonization::TriangularMesh>(res));
    const Polygonization::TriangularMesh &mesh =
      std::get<Polygonization::TriangularMesh>(res);

    for (const auto &[i0, i1, i2] : mesh.triangles) {
      for (int idx : {i0, i1, i2}) {
        CHECK(idx >= 0 && idx < (int)mesh.vertices.size())
            << "Vertex index out of bounds";
      }

      const vec2 v0 = mesh.vertices[i0];
      const vec2 v1 = mesh.vertices[i1];
      const vec2 v2 = mesh.vertices[i2];

      double area =
          (v1.x - v0.x) * (v2.y - v0.y) - (v2.x - v0.x) * (v1.y - v0.y);
      CHECK(area < -1e-10) << "Triangle is degenerate or incorrectly oriented";
    }

    for (const auto &[i0, i1, i2] : mesh.triangles) {
      const std::array<int, 3> poly = {i0, i1, i2};
      for (int j = 0; j < 3; j++) {
        int next = (j + 1) % 3;
        const auto &v1 = mesh.vertices[poly[j]];
        const auto &v2 = mesh.vertices[poly[next]];
        img.BlendLine32((int)v1.x, (int)v1.y, (int)v2.x, (int)v2.y, 0xFF000080);
      }
    }

    for (const auto &path : shape.points) {
      for (int j = 0; j < (int)path.size(); j++) {
        int next = (j + 1) % path.size();
        const auto &v1 = path[j];
        const auto &v2 = path[next];
        img.BlendLine32((int)v1.x, (int)v1.y, (int)v2.x, (int)v2.y, 0xFFFFFFAA);
      }
    }

    img.Save(std::format("triangulation-test-{}.png", i));
  }
}

static void DebugDrawPolygonize() {
  for (int max_vertices : {4, 6, 9}) {
    ArcFour rc("test");

    for (int i = 0; i < 10; i++) {
      ImageRGBA img(kWidth, kHeight);
      img.Clear32(0x000000FF);

      Polygonization::Shape shape = GenerateShape(i, &rc);

      Polygonization::PolygonizeResult res =
        Polygonization::Polygonize(shape, max_vertices);
      if (const std::string_view *err = std::get_if<std::string_view>(&res)) {
        LOG(FATAL) << *err;
      }
      CHECK(std::holds_alternative<Polygonization::Mesh>(res));
      const Polygonization::Mesh &mesh =
        std::get<Polygonization::Mesh>(res);

      for (const auto &poly : mesh.polygons) {
        for (int j = 0; j < (int)poly.size(); j++) {
          int next = (j + 1) % poly.size();
          const auto &v1 = mesh.vertices[poly[j]];
          const auto &v2 = mesh.vertices[poly[next]];
          img.BlendLine32((int)v1.x, (int)v1.y, (int)v2.x, (int)v2.y,
                          0xFF000080);
        }
      }

      for (const auto &path : shape.points) {
        for (int j = 0; j < (int)path.size(); j++) {
          int next = (j + 1) % path.size();
          const auto &v1 = path[j];
          const auto &v2 = path[next];
          img.BlendLine32((int)v1.x, (int)v1.y, (int)v2.x, (int)v2.y,
                          0xFFFFFFAA);
        }
      }

      img.Save(std::format("polygonization-test-{}-{}.png", max_vertices, i));
    }
  }
}

int main(int argc, char **argv) {
  ANSI::Init();

  DebugDrawTriangulate();
  DebugDrawPolygonize();

  Print("OK\n");
  return 0;
}
