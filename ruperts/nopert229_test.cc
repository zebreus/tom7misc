// nopert229_test.cc: Tests for Nopert candidate #229 geometry,
// verifying double vs. rational coordinate agreement and C5 rotational symmetry.

#include "nopert229.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <numbers>
#include <vector>

#include "base/logging.h"
#include "bignum/big.h"
#include "bignum/big-overloads.h"
#include "yocto-math.h"

using vec3 = yocto::vec<double, 3>;

// Test 1: Verify that double and rational coordinates agree to within floating-point precision.
static void TestDoubleRationalAgreement() {
  std::cout << "[RUN] TestDoubleRationalAgreement\n";
  const auto &vs_q = GetVerticesQ();

  double max_err = 0.0;
  for (int v = 0; v < NUM_VERTICES; v++) {
    vec3 v_double = Vertex(v);
    CHECK_EQ(v_double.x, VERTICES[v][0]);
    CHECK_EQ(v_double.y, VERTICES[v][1]);
    CHECK_EQ(v_double.z, VERTICES[v][2]);

    double qx = vs_q[v][0].ToDouble();
    double qy = vs_q[v][1].ToDouble();
    double qz = vs_q[v][2].ToDouble();

    double err_x = std::abs(v_double.x - qx);
    double err_y = std::abs(v_double.y - qy);
    double err_z = std::abs(v_double.z - qz);

    max_err = std::max(max_err, std::max({err_x, err_y, err_z}));

    // Each rational coordinate is specified with 15-16 decimal digits,
    // so agreement with double should be well within 1e-14.
    CHECK_LT(err_x, 1e-14);
    CHECK_LT(err_y, 1e-14);
    CHECK_LT(err_z, 1e-14);
  }

  std::cout << "  Max coordinate discrepancy |double - Q.ToDouble| = " << max_err << "\n";
  CHECK_LT(max_err, 1e-15);
  std::cout << "[PASS] TestDoubleRationalAgreement\n";
}

// Test 2: Verify C5 rotational symmetry around the z-axis.
static void TestC5Symmetry() {
  std::cout << "[RUN] TestC5Symmetry\n";
  const auto &vs_q = GetVerticesQ();

  // Angle theta = 2*pi / 5 (72 degrees)
  constexpr double angle = 2.0 * std::numbers::pi / 5.0;
  const double cos_theta = std::cos(angle);
  const double sin_theta = std::sin(angle);

  // 1. Check orbit rotation in double precision:
  // Polyhedron #229 has 5 orbits (k = 0..4) of 4 seeds (s = 0..3): vertex index = 4*k + s.
  // Rotating orbit k by +2*pi/5 around the z-axis aligns with orbit (k + 1) % 5.
  double max_rot_err = 0.0;
  for (int k = 0; k < 5; k++) {
    int next_k = (k + 1) % 5;
    for (int s = 0; s < 4; s++) {
      int idx_curr = 4 * k + s;
      int idx_next = 4 * next_k + s;

      vec3 curr = Vertex(idx_curr);
      vec3 expected_next = Vertex(idx_next);

      // Rotate curr around z-axis by +72 degrees
      vec3 rotated = {
        cos_theta * curr.x - sin_theta * curr.y,
        sin_theta * curr.x + cos_theta * curr.y,
        curr.z
      };

      double dist = yocto::length(rotated - expected_next);
      max_rot_err = std::max(max_rot_err, dist);
      CHECK_LT(dist, 1e-14);
    }
  }
  std::cout << "  Max C5 rotation error across all orbits: " << max_rot_err << "\n";
  CHECK_LT(max_rot_err, 1e-15);

  // 2. Check that the z-coordinates are IDENTICAL across orbits for each seed.
  // Both in double precision and in exact BigRat rational representation!
  for (int s = 0; s < 4; s++) {
    double z_ref_d = VERTICES[s][2];
    const BigRat &z_ref_q = vs_q[s][2];

    for (int k = 1; k < 5; k++) {
      int idx = 4 * k + s;
      CHECK_EQ(VERTICES[idx][2], z_ref_d);
      CHECK(vs_q[idx][2] == z_ref_q);
    }
  }
  std::cout << "  Exact z-coordinate equality verified across all 5 orbits for all 4 seeds.\n";

  // 3. Check cylindrical radius r_xy = sqrt(x^2 + y^2) is constant across orbits for each seed.
  for (int s = 0; s < 4; s++) {
    vec3 v0 = Vertex(s);
    double r0 = std::hypot(v0.x, v0.y);
    for (int k = 1; k < 5; k++) {
      vec3 vk = Vertex(4 * k + s);
      double rk = std::hypot(vk.x, vk.y);
      double diff = std::abs(rk - r0);
      CHECK_LT(diff, 1e-14);
    }
  }
  std::cout << "  Cylindrical radius invariance verified across all 5 orbits.\n";

  // 4. GoodPoly invariant: All vertices strictly inside unit sphere ||v|| < 1.
  for (int v = 0; v < NUM_VERTICES; v++) {
    vec3 pt = Vertex(v);
    double norm = yocto::length(pt);
    CHECK_LT(norm, 1.0);
    CHECK_GT(norm, 0.0);
  }
  std::cout << "  GoodPoly invariant (0 < ||v|| < 1) verified for all 20 vertices.\n";
  std::cout << "[PASS] TestC5Symmetry\n";
}

// Test 3: Verify GetVerticesQ singleton behavior (address stability & non-null).
static void TestSingleton() {
  std::cout << "[RUN] TestSingleton\n";
  const auto &ref1 = GetVerticesQ();
  const auto &ref2 = GetVerticesQ();
  CHECK_EQ(&ref1, &ref2);
  CHECK_EQ(ref1.size(), 20u);
  std::cout << "[PASS] TestSingleton\n";
}

int main(int argc, char **argv) {
  std::cout << "=== Running Nopert #229 Geometry Unit Tests ===\n";
  TestDoubleRationalAgreement();
  TestC5Symmetry();
  TestSingleton();
  std::cout << "=== All Nopert #229 Tests Passed! ===\n";
  return 0;
}
