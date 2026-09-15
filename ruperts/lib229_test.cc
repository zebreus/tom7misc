#include "lib229.h"
#include "base/print.h"
#include "ansi.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <string_view>
#include <vector>

// Direct evaluation of quadratic polynomial:
// P(x,y,z) = C0 + C1*x + C2*y + C3*z + C4*x^2 + C5*xy + C6*xz + C7*y^2 + C8*yz + C9*z^2
static inline double EvalPolyDirect(const double C[10], double x, double y, double z) {
  return C[0] + C[1]*x + C[2]*y + C[3]*z +
         C[4]*x*x + C[5]*x*y + C[6]*x*z +
         C[7]*y*y + C[8]*y*z + C[9]*z*z;
}

// 1D Bernstein basis functions of degree 2 on [0, 1]
static inline double B0(double t) { return (1.0 - t) * (1.0 - t); }
static inline double B1(double t) { return 2.0 * t * (1.0 - t); }
static inline double B2(double t) { return t * t; }

// Direct evaluation of the 3D tensored Bernstein expansion at local coordinates (u, v, w) in [0, 1]^3
static inline double EvalBernsteinDirect(const double c[27], double u, double v, double w) {
  double bu[3] = {B0(u), B1(u), B2(u)};
  double bv[3] = {B0(v), B1(v), B2(v)};
  double bw[3] = {B0(w), B1(w), B2(w)};

  double sum = 0.0;
  int idx = 0;
  for (int bi = 0; bi <= 2; bi++) {
    for (int bj = 0; bj <= 2; bj++) {
      for (int bk = 0; bk <= 2; bk++) {
        sum += c[idx++] * bu[bi] * bv[bj] * bw[bk];
      }
    }
  }
  return sum;
}

static int g_failed_tests = 0;

#define CHECK_TOL(actual, expected, tol, desc) do { \
  double diff = std::abs((actual) - (expected)); \
  if (diff > (tol)) { \
    Print(ARED("FAIL: ") "{}: actual={:.17g}, expected={:.17g}, diff={:.3e} > tol={:.3e}\n", \
          (desc), (actual), (expected), diff, (tol)); \
    g_failed_tests++; \
  } \
} while (0)

#define CHECK_TRUE(cond, desc) do { \
  if (!(cond)) { \
    Print(ARED("FAIL: ") "{}\n", (desc)); \
    g_failed_tests++; \
  } \
} while (0)

// Test 1: Constant polynomial
void TestConstantPolynomial() {
  Print("Running TestConstantPolynomial...\n");
  double C[10] = {42.123456789012345, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  double lx = -1.2, ly = 0.5, lz = 3.1;
  double wx = 0.4, wy = 0.8, wz = 0.2;

  double ctrl[27];
  ComputeBernstein27Controls(C, lx, ly, lz, wx, wy, wz, ctrl);

  for (int i = 0; i < 27; i++) {
    CHECK_TOL(ctrl[i], C[0], 1e-14, "Constant control point value");
  }

  double min_val = Bernstein27Min(C, lx, ly, lz, wx, wy, wz);
  CHECK_TOL(min_val, C[0], 1e-14, "Constant polynomial Bernstein27Min");
}

// Test 2: Linear polynomials along all axes
void TestLinearPolynomial() {
  Print("Running TestLinearPolynomial...\n");
  // P(x, y, z) = 1.5 + 2.5*x - 3.25*y + 4.75*z
  double C[10] = {1.5, 2.5, -3.25, 4.75, 0, 0, 0, 0, 0, 0};
  double lx = 0.1, ly = -0.2, lz = 0.3;
  double wx = 0.05, wy = 0.04, wz = 0.08;

  double ctrl[27];
  ComputeBernstein27Controls(C, lx, ly, lz, wx, wy, wz, ctrl);

  // Check 8 corners
  for (int bi = 0; bi <= 2; bi += 2) {
    for (int bj = 0; bj <= 2; bj += 2) {
      for (int bk = 0; bk <= 2; bk += 2) {
        int idx = 9 * bi + 3 * bj + bk;
        double x = lx + (bi == 2 ? wx : 0.0);
        double y = ly + (bj == 2 ? wy : 0.0);
        double z = lz + (bk == 2 ? wz : 0.0);
        double expected = EvalPolyDirect(C, x, y, z);
        CHECK_TOL(ctrl[idx], expected, 1e-14, "Linear corner interpolation");
      }
    }
  }

  // Linear exactness: midpoint controls must equal exact average of endpoints
  for (int bi = 0; bi <= 2; bi++) {
    for (int bj = 0; bj <= 2; bj++) {
      int idx0 = 9 * bi + 3 * bj + 0;
      int idx1 = 9 * bi + 3 * bj + 1;
      int idx2 = 9 * bi + 3 * bj + 2;
      double expected_mid = 0.5 * (ctrl[idx0] + ctrl[idx2]);
      CHECK_TOL(ctrl[idx1], expected_mid, 1e-14, "Linear z-midpoint average");
    }
  }

  // Equivalence with Bernstein27Min
  double min_ctrl = *std::min_element(ctrl, ctrl + 27);
  double b_min = Bernstein27Min(C, lx, ly, lz, wx, wy, wz);
  CHECK_TOL(b_min, min_ctrl, 1e-15, "Linear Bernstein27Min match");
}

// Test 3: Pure quadratics (x^2, y^2, z^2)
void TestPureQuadratics() {
  Print("Running TestPureQuadratics...\n");
  // P(x, y, z) = 3.0*x^2 - 2.0*y^2 + 5.0*z^2
  double C[10] = {0, 0, 0, 0, 3.0, 0, 0, -2.0, 0, 5.0};
  double lx = -0.5, ly = 0.2, lz = -0.1;
  double wx = 0.4, wy = 0.3, wz = 0.6;

  double ctrl[27];
  ComputeBernstein27Controls(C, lx, ly, lz, wx, wy, wz, ctrl);

  // Check 8 corners
  for (int bi = 0; bi <= 2; bi += 2) {
    for (int bj = 0; bj <= 2; bj += 2) {
      for (int bk = 0; bk <= 2; bk += 2) {
        int idx = 9 * bi + 3 * bj + bk;
        double x = lx + (bi == 2 ? wx : 0.0);
        double y = ly + (bj == 2 ? wy : 0.0);
        double z = lz + (bk == 2 ? wz : 0.0);
        double expected = EvalPolyDirect(C, x, y, z);
        CHECK_TOL(ctrl[idx], expected, 1e-13, "Pure quadratic corner interpolation");
      }
    }
  }

  // Dense reconstruction test across [0, 1]^3
  double max_err = 0.0;
  for (int i = 0; i <= 8; i++) {
    double u = i / 8.0;
    double x = lx + u * wx;
    for (int j = 0; j <= 8; j++) {
      double v = j / 8.0;
      double y = ly + v * wy;
      for (int k = 0; k <= 8; k++) {
        double w = k / 8.0;
        double z = lz + w * wz;
        double p_exact = EvalPolyDirect(C, x, y, z);
        double p_bern = EvalBernsteinDirect(ctrl, u, v, w);
        max_err = std::max(max_err, std::abs(p_exact - p_bern));
      }
    }
  }
  CHECK_TOL(max_err, 0.0, 1e-13, "Pure quadratic dense reconstruction");
}

// Test 4: General quadratic with cross terms and Cayley-scale coordinates
void TestGeneralQuadraticCayleyScale() {
  Print("Running TestGeneralQuadraticCayleyScale...\n");
  // Realistic coefficients from Candidate #229 contact polynomials
  double C[10] = {
    1.234567e-4,  // C0
    -0.854321,    // C1 (dx)
     0.643210,    // C2 (dy)
    -0.432109,    // C3 (dz)
     2.154321,    // C4 (dx^2)
    -1.432109,    // C5 (dx*dy)
     0.987654,    // C6 (dx*dz)
     1.876543,    // C7 (dy^2)
    -0.765432,    // C8 (dy*dz)
     2.345678     // C9 (dz^2)
  };

  // Cayley box parameters (depth 32 box)
  double lx = 0.00048828125, ly = -0.00048828125, lz = -0.0009765625;
  double wx = 0.0009765625, wy = 0.0009765625, wz = 0.0006510416666666666;

  double ctrl[27];
  ComputeBernstein27Controls(C, lx, ly, lz, wx, wy, wz, ctrl);

  // 1. Exact 8-corner interpolation
  for (int bi = 0; bi <= 2; bi += 2) {
    for (int bj = 0; bj <= 2; bj += 2) {
      for (int bk = 0; bk <= 2; bk += 2) {
        int idx = 9 * bi + 3 * bj + bk;
        double x = lx + (bi == 2 ? wx : 0.0);
        double y = ly + (bj == 2 ? wy : 0.0);
        double z = lz + (bk == 2 ? wz : 0.0);
        double expected = EvalPolyDirect(C, x, y, z);
        CHECK_TOL(ctrl[idx], expected, 1e-13, "General quadratic corner interpolation");
      }
    }
  }

  // 2. High-precision reconstruction test across 10x10x10 grid in box
  double max_recon_err = 0.0;
  double min_ctrl = ctrl[0], max_ctrl = ctrl[0];
  for (int i = 0; i < 27; i++) {
    min_ctrl = std::min(min_ctrl, ctrl[i]);
    max_ctrl = std::max(max_ctrl, ctrl[i]);
  }

  for (int i = 0; i <= 10; i++) {
    double u = i / 10.0;
    double x = lx + u * wx;
    for (int j = 0; j <= 10; j++) {
      double v = j / 10.0;
      double y = ly + v * wy;
      for (int k = 0; k <= 10; k++) {
        double w = k / 10.0;
        double z = lz + w * wz;
        double p_exact = EvalPolyDirect(C, x, y, z);
        double p_bern = EvalBernsteinDirect(ctrl, u, v, w);
        max_recon_err = std::max(max_recon_err, std::abs(p_exact - p_bern));

        // 3. Convex hull property check: min_ctrl <= p_exact <= max_ctrl
        CHECK_TRUE(p_exact >= min_ctrl - 1e-14, "Convex hull lower bound violation");
        CHECK_TRUE(p_exact <= max_ctrl + 1e-14, "Convex hull upper bound violation");
      }
    }
  }
  CHECK_TOL(max_recon_err, 0.0, 1e-13, "General quadratic dense reconstruction");

  // 4. Exact match with Bernstein27Min
  double b_min = Bernstein27Min(C, lx, ly, lz, wx, wy, wz);
  CHECK_TOL(b_min, min_ctrl, 1e-15, "General quadratic Bernstein27Min match");
}

// Test 5: Multiple randomized polynomials with various box scales
void TestRandomizedPolynomialSuites() {
  Print("Running TestRandomizedPolynomialSuites (50 cases)...\n");
  srand(229);

  auto RandDbl = [](double lo, double hi) {
    return lo + (hi - lo) * ((double)rand() / (double)RAND_MAX);
  };

  for (int trial = 0; trial < 50; trial++) {
    double C[10];
    for (int m = 0; m < 10; m++) {
      C[m] = RandDbl(-10.0, 10.0);
    }
    double lx = RandDbl(-1.0, 1.0);
    double ly = RandDbl(-1.0, 1.0);
    double lz = RandDbl(-1.0, 1.0);
    double wx = RandDbl(1e-5, 0.5);
    double wy = RandDbl(1e-5, 0.5);
    double wz = RandDbl(1e-5, 0.5);

    double ctrl[27];
    ComputeBernstein27Controls(C, lx, ly, lz, wx, wy, wz, ctrl);

    // Verify 8 corners
    for (int bi = 0; bi <= 2; bi += 2) {
      for (int bj = 0; bj <= 2; bj += 2) {
        for (int bk = 0; bk <= 2; bk += 2) {
          int idx = 9 * bi + 3 * bj + bk;
          double x = lx + (bi == 2 ? wx : 0.0);
          double y = ly + (bj == 2 ? wy : 0.0);
          double z = lz + (bk == 2 ? wz : 0.0);
          double expected = EvalPolyDirect(C, x, y, z);
          double diff = std::abs(ctrl[idx] - expected);
          if (diff > 1e-11) {
            Print(ARED("FAIL in trial {}: corner diff={:.3e}\n"), trial, diff);
            g_failed_tests++;
          }
        }
      }
    }

    // Verify Bernstein27Min equivalence
    double min_ctrl = *std::min_element(ctrl, ctrl + 27);
    double b_min = Bernstein27Min(C, lx, ly, lz, wx, wy, wz);
    if (std::abs(b_min - min_ctrl) > 1e-14) {
      Print(ARED("FAIL in trial {}: Bernstein27Min mismatch\n"), trial);
      g_failed_tests++;
    }
  }
}

int main(int argc, char **argv) {
  Print(ACYAN("=== Running lib229 Bernstein Unit Tests ===\n"));

  TestConstantPolynomial();
  TestLinearPolynomial();
  TestPureQuadratics();
  TestGeneralQuadraticCayleyScale();
  TestRandomizedPolynomialSuites();

  if (g_failed_tests == 0) {
    Print(AGREEN("\nALL BERNSTEIN TESTS PASSED WITH HIGH PRECISION!\n"));
    return 0;
  } else {
    Print(ARED("\nFAILED {} TESTS!\n"), g_failed_tests);
    return 1;
  }
}
