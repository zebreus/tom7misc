
#include "nasty.h"

#include <cmath>
#include <numbers>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "geom/polyhedra.h"

static constexpr double pi = std::numbers::pi;

Polyhedron Nasty::TiltedDecagonPyramid() {
  std::vector<vec3> vertices;
  vertices.reserve(11);

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

Polyhedron Nasty::SquatSnail() {
  std::vector<vec3> vertices;
  vertices.reserve(11);

  // Standard decagon base
  for (int i = 0; i < 10; i++) {
    double angle = i * (2.0 * pi / 10.0);
    vertices.push_back(vec3{std::cos(angle), std::sin(angle), 0.0});
  }

  vertices.push_back(vec3{0.95, 0.0, 0.05});

  std::optional<Polyhedron> opt =
      PolyhedronFromConvexVertices(std::move(vertices), "squatsnail");
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

Polyhedron Nasty::Cigar() {
  std::vector<vec3> vertices;

  // Tunable parameter: dictates the topological diameter of the shape.
  // num_layers = 6 yields 30 faces.
  // num_layers = 10 yields 54 faces.
  const int num_layers = 8;
  vertices.reserve(num_layers * 3 + 2);

  // Dimensions of the cigar
  const double length = 10.0; // Z-axis stretch
  const double radius = 2.0;  // X/Y-axis max thickness

  // Top Pole
  vertices.push_back(vec3{0.0, 0.0, length});

  // Intermediate triangular rings
  for (int i = 1; i < num_layers; i++) {
    // Map i to an angle between 0 and PI for spherical distribution
    double theta = pi * (double)i / (double)num_layers;

    // Ellipsoid profile
    double z = length * std::cos(theta);
    double r = radius * std::sin(theta);

    // Twist every other layer by 60 degrees to ensure strictly convex
    // triangular faces (forming an antiprism stack)
    double twist = (i % 2) * (pi / 3.0);

    for (int j = 0; j < 3; j++) {
      double angle = (double)j * (2.0 * pi / 3.0) + twist;
      vertices.push_back(vec3{r * std::cos(angle), r * std::sin(angle), z});
    }
  }

  // Bottom Pole
  vertices.push_back(vec3{0.0, 0.0, -length});

  std::optional<Polyhedron> opt = PolyhedronFromConvexVertices(
      std::move(vertices), "cigar");
  CHECK(opt.has_value());
  return std::move(opt.value());
}

Polyhedron Nasty::RubiksCube() {
  std::vector<vec3> vertices;
  vertices.reserve(56);

  // Distort the Rubik's cube to make it strictly convex. By using these
  // specific values, the 4 vertices of each small face remain exactly
  // coplanar, so the resulting polyhedron has exactly 54 faces.
  for (int x = -3; x <= 3; x += 2) {
    for (int y = -3; y <= 3; y += 2) {
      for (int z = -3; z <= 3; z += 2) {
        int num_large = (std::abs(x) == 3 ? 1 : 0) +
                        (std::abs(y) == 3 ? 1 : 0) +
                        (std::abs(z) == 3 ? 1 : 0);
        if (num_large == 0) continue;

        double l_val = 0.0, s_val = 0.0;
        if (num_large == 1) {
          l_val = 3.0;
          s_val = 1.0;
        } else if (num_large == 2) {
          l_val = 2.95;
          s_val = 1.05;
        } else {
          l_val = 61.0 / 21.0;
        }

        auto map_coord = [l_val, s_val](int c) {
          double mag = (std::abs(c) == 3) ? l_val : s_val;
          return c < 0 ? -mag : mag;
        };

        vertices.push_back(vec3{map_coord(x), map_coord(y), map_coord(z)});
      }
    }
  }

  std::optional<Polyhedron> opt = PolyhedronFromConvexVertices(
      std::move(vertices), "rubikscube");
  CHECK(opt.has_value());

  return std::move(opt.value());
}


std::optional<Polyhedron> Nasty::ByName(std::string_view name) {
  if (name == "tilteddecagonpyramid") return TiltedDecagonPyramid();
  if (name == "squatsnail") return SquatSnail();
  if (name == "flattenedicosahedron") return FlattenedIcosahedron();
  if (name == "longtaperedprism") return LongTaperedPrism();
  if (name == "longtaperedantiprism") return LongTaperedAntiprism();
  if (name == "lens") return Lens();
  if (name == "lowpolylens") return LowPolyLens();
  if (name == "coin") return Coin();
  if (name == "sawblade") return Sawblade();
  if (name == "dome") return Dome();
  if (name == "chisel") return Chisel();
  if (name == "cigar") return Cigar();
  if (name == "rubikscube") return RubiksCube();
  return std::nullopt;
}
