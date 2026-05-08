
#include "nasty.h"

#include <cmath>
#include <optional>
#include <utility>
#include <vector>

#include "geom/polyhedra.h"

Polyhedron Nasty::TiltedDecagonPyramid() {
  std::vector<vec3> vertices;
  vertices.reserve(11);

  const double pi = std::acos(-1.0);
  for (int i = 0; i < 10; i++) {
    double angle = i * (2.0 * pi / 10.0);
    vertices.push_back(vec3{std::cos(angle), std::sin(angle), 0.0});
  }
  vertices.push_back(vec3{7.0, 11.0, 100.0});

  std::optional<Polyhedron> opt =
      PolyhedronFromConvexVertices(std::move(vertices),
                                   "tilteddecagonpyramid");
  CHECK(opt.has_value());

  return std::move(opt.value());

}

Polyhedron Nasty::FlattenedIcosahedron() {
  std::vector<vec3> flattened = Icosahedron().vertices;
  for (vec3 &v : flattened) {
    v.z /= 100.0;
  }

  std::optional<Polyhedron> opt =
      PolyhedronFromConvexVertices(std::move(flattened),
                                   "flattenedicosahedron");
  CHECK(opt.has_value());

  return std::move(opt.value());
}

Polyhedron Nasty::LongTaperedPrism() {
  std::vector<vec3> vertices;
  vertices.reserve(20);

  const double pi = std::acos(-1.0);
  for (int i = 0; i < 10; i++) {
    double angle = (double)i * (2.0 * pi / 10.0);
    double c = std::cos(angle);
    double s = std::sin(angle);
    vertices.push_back(vec3{2.0 * c, 2.0 * s, 0.0});
    vertices.push_back(vec3{1.0 * c, 1.0 * s, 100.0});
  }

  std::optional<Polyhedron> opt = PolyhedronFromConvexVertices(
      std::move(vertices), "longtaperedprism");
  CHECK(opt.has_value());

  return std::move(opt.value());
}

Polyhedron Nasty::LongTaperedAntiprism() {
  std::vector<vec3> vertices;
  vertices.reserve(20);

  const double pi = std::acos(-1.0);
  for (int i = 0; i < 10; i++) {
    double angle1 = (double)i * (2.0 * pi / 10.0);
    vertices.push_back(
        vec3{2.0 * std::cos(angle1), 2.0 * std::sin(angle1), 0.0});

    double angle2 = angle1 + (pi / 10.0);
    vertices.push_back(
        vec3{1.0 * std::cos(angle2), 1.0 * std::sin(angle2), 100.0});
  }

  std::optional<Polyhedron> opt = PolyhedronFromConvexVertices(
      std::move(vertices), "longtaperedantiprism");
  CHECK(opt.has_value());

  return std::move(opt.value());
}

Polyhedron Nasty::Lens() {
  std::vector<vec3> vertices;
  vertices.reserve(212);

  const double pi = std::acos(-1.0);
  const double r_sphere = 2.0;
  const double r_eq = 1.0;
  const double z_offset = std::sqrt(r_sphere * r_sphere - r_eq * r_eq);

  const int num_rings = 4;
  for (int i = 0; i <= num_rings; i++) {
    double max_phi = std::asin(r_eq / r_sphere);
    double phi = (double)i * (max_phi / (double)num_rings);
    double r = r_sphere * std::sin(phi);
    double z = (i == num_rings) ? 0.0 : r_sphere * std::cos(phi) - z_offset;

    if (i == 0) {
      vertices.push_back(vec3{0.0, 0.0, z});
      vertices.push_back(vec3{0.0, 0.0, -z});
    } else {
      const int num_points = 30;
      for (int j = 0; j < num_points; j++) {
        double theta = ((double)j + (double)(i % 2) * 0.5) *
            (2.0 * pi / (double)num_points);
        double c = std::cos(theta);
        double s = std::sin(theta);
        vertices.push_back(vec3{r * c, r * s, z});
        if (z > 1e-5) {
          vertices.push_back(vec3{r * c, r * s, -z});
        }
      }
    }
  }

  std::optional<Polyhedron> opt = PolyhedronFromConvexVertices(
      std::move(vertices), "lens");
  CHECK(opt.has_value());

  return std::move(opt.value());
}

Polyhedron Nasty::LowPolyLens() {
  std::vector<vec3> vertices;
  vertices.reserve(42);

  const double pi = std::acos(-1.0);
  // Extremely flat top and bottom poles
  vertices.push_back(vec3{0.0, 0.0, 0.2});
  vertices.push_back(vec3{0.0, 0.0, -0.2});

  for (int i = 0; i < 20; i++) {
    double angle = (double)i * (2.0 * pi / 20.0);
    double c = std::cos(angle);
    double s = std::sin(angle);

    // Sharp equator
    vertices.push_back(vec3{2.0 * c, 2.0 * s, 0.0});
    // Intermediate shallow rings
    vertices.push_back(vec3{1.4 * c, 1.4 * s, 0.1});
    vertices.push_back(vec3{1.4 * c, 1.4 * s, -0.1});
  }

  std::optional<Polyhedron> opt = PolyhedronFromConvexVertices(
      std::move(vertices), "lowpolylens");
  CHECK(opt.has_value());
  return std::move(opt.value());
}

Polyhedron Nasty::Coin() {
  std::vector<vec3> vertices;
  vertices.reserve(60);

  const double pi = std::acos(-1.0);
  for (int i = 0; i < 30; i++) {
    double angle = (double)i * (2.0 * pi / 30.0);
    double c = std::cos(angle);
    double s = std::sin(angle);
    vertices.push_back(vec3{c, s, -0.005});
    vertices.push_back(vec3{c, s, 0.005});
  }

  std::optional<Polyhedron> opt = PolyhedronFromConvexVertices(
      std::move(vertices), "coin");
  CHECK(opt.has_value());

  return std::move(opt.value());
}

Polyhedron Nasty::Sawblade() {
  std::vector<vec3> vertices;
  vertices.reserve(40);

  const double pi = std::acos(-1.0);
  for (int i = 0; i < 20; i++) {
    double angle1 = (double)i * (2.0 * pi / 20.0);
    vertices.push_back(
        vec3{2.0 * std::cos(angle1), 2.0 * std::sin(angle1), 0.0});

    double angle2 = angle1 + (pi / 20.0);
    // Almost same radius, tiny Z difference
    vertices.push_back(
        vec3{1.95 * std::cos(angle2), 1.95 * std::sin(angle2), 0.05});
  }

  std::optional<Polyhedron> opt = PolyhedronFromConvexVertices(
      std::move(vertices), "sawblade");
  CHECK(opt.has_value());
  return std::move(opt.value());
}

Polyhedron Nasty::Dome() {
  std::vector<vec3> vertices;
  vertices.reserve(33);

  const double pi = std::acos(-1.0);
  vertices.push_back(vec3{0.0, 0.0, 2.0}); // Pole

  for (int i = 0; i < 16; i++) {
    double angle = (double)i * (2.0 * pi / 16.0);
    double c = std::cos(angle);
    double s = std::sin(angle);

    // Equator (Base)
    vertices.push_back(vec3{2.0 * c, 2.0 * s, 0.0});
    // 45-degree latitude
    vertices.push_back(vec3{1.414 * c, 1.414 * s, 1.414});
  }

  std::optional<Polyhedron> opt = PolyhedronFromConvexVertices(
      std::move(vertices), "dome");
  CHECK(opt.has_value());
  return std::move(opt.value());
}

Polyhedron Nasty::Chisel() {
  std::vector<vec3> vertices;
  vertices.reserve(32);

  const double pi = std::acos(-1.0);

  // Base is a wide, flat 16-gon (an ellipse)
  for (int i = 0; i < 16; i++) {
    double angle = (double)i * (2.0 * pi / 16.0);
    vertices.push_back(vec3{10.0 * std::cos(angle), 1.0 * std::sin(angle), 0.0});
  }

  // Top is a tiny 16-gon (an ellipse) pinched almost into a line
  for (int i = 0; i < 16; i++) {
    double angle = (double)i * (2.0 * pi / 16.0);
    vertices.push_back(vec3{0.1 * std::cos(angle), 0.01 * std::sin(angle), 10.0});
  }

  std::optional<Polyhedron> opt = PolyhedronFromConvexVertices(
      std::move(vertices), "chisel");
  CHECK(opt.has_value());
  return std::move(opt.value());
}
