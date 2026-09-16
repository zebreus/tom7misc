#include "lib229.h"
#include "base/print.h"
#include "ansi.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <fstream>
#include <string_view>
#include <unistd.h>
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

void TestAnalyticalSplittingRule() {
  Print("Running TestAnalyticalSplittingRule...\n");

  // 1. Hard depth constraints
  CHECK_TRUE(!ShouldSplitBox(/*box_depth=*/40, /*max_box_depth=*/40,
                             /*view_depth=*/5, /*max_view_depth=*/14,
                             /*box_span=*/1.0, /*view_penalty=*/0.1,
                             /*rot_diam=*/0.5, /*view_diam=*/0.1),
             "Max box depth forces view split (false)");

  CHECK_TRUE(ShouldSplitBox(/*box_depth=*/30, /*max_box_depth=*/40,
                            /*view_depth=*/14, /*max_view_depth=*/14,
                            /*box_span=*/0.01, /*view_penalty=*/1.0,
                            /*rot_diam=*/0.01, /*view_diam=*/0.5),
             "Max view depth forces box split (true)");

  // 2. Pre-split root lock
  CHECK_TRUE(ShouldSplitBox(/*box_depth=*/30, /*max_box_depth=*/40,
                            /*view_depth=*/8, /*max_view_depth=*/14,
                            /*box_span=*/0.01, /*view_penalty=*/1.0,
                            /*rot_diam=*/0.01, /*view_diam=*/0.5,
                            /*split_kappa=*/0.5, /*pre_vsplits=*/2, /*root_view_depth=*/6),
             "Pre-split depth cap forces box split (true)");

  // 3. Analytical Geometry-Specific Rule:
  // Rotation error dominates -> split box
  CHECK_TRUE(ShouldSplitBox(/*box_depth=*/30, /*max_box_depth=*/40,
                            /*view_depth=*/6, /*max_view_depth=*/14,
                            /*box_span=*/0.05, /*view_penalty=*/0.01,
                            /*rot_diam=*/0.01, /*view_diam=*/0.5),
             "Analytical rule: box_span > view_penalty selects box split");

  // View defect error dominates -> split view
  CHECK_TRUE(!ShouldSplitBox(/*box_depth=*/30, /*max_box_depth=*/40,
                             /*view_depth=*/6, /*max_view_depth=*/14,
                             /*box_span=*/0.01, /*view_penalty=*/0.05,
                             /*rot_diam=*/0.5, /*view_diam=*/0.1),
             "Analytical rule: view_penalty > box_span selects view split");

  // Boundary equality -> favor box split (branching factor 2)
  CHECK_TRUE(ShouldSplitBox(/*box_depth=*/30, /*max_box_depth=*/40,
                            /*view_depth=*/6, /*max_view_depth=*/14,
                            /*box_span=*/0.02, /*view_penalty=*/0.02,
                            /*rot_diam=*/0.01, /*view_diam=*/0.5),
             "Analytical rule: box_span == view_penalty favors box split");

  // 4. Fallback aspect ratio (kappa)
  // rot_diam >= kappa * view_diam -> split box
  CHECK_TRUE(ShouldSplitBox(/*box_depth=*/30, /*max_box_depth=*/40,
                            /*view_depth=*/6, /*max_view_depth=*/14,
                            /*box_span=*/0.0, /*view_penalty=*/0.0,
                            /*rot_diam=*/0.06, /*view_diam=*/0.10,
                            /*split_kappa=*/0.5),
             "Fallback kappa: rot_diam >= kappa * view_diam selects box split");

  // rot_diam < kappa * view_diam with pacing
  CHECK_TRUE(ShouldSplitBox(/*box_depth=*/30, /*max_box_depth=*/40,
                            /*view_depth=*/6, /*max_view_depth=*/14,
                            /*box_span=*/0.0, /*view_penalty=*/0.0,
                            /*rot_diam=*/0.02, /*view_diam=*/0.10,
                            /*split_kappa=*/0.5, /*pre_vsplits=*/0, /*root_view_depth=*/0,
                            /*box_splits_since_view=*/0),
             "Fallback kappa: pacing box_splits_since_view=0 forces box split");

  CHECK_TRUE(ShouldSplitBox(/*box_depth=*/30, /*max_box_depth=*/40,
                            /*view_depth=*/6, /*max_view_depth=*/14,
                            /*box_span=*/0.0, /*view_penalty=*/0.0,
                            /*rot_diam=*/0.02, /*view_diam=*/0.10,
                            /*split_kappa=*/0.5, /*pre_vsplits=*/0, /*root_view_depth=*/0,
                            /*box_splits_since_view=*/1),
             "Fallback kappa: pacing box_splits_since_view=1 forces box split");

  CHECK_TRUE(!ShouldSplitBox(/*box_depth=*/30, /*max_box_depth=*/40,
                             /*view_depth=*/6, /*max_view_depth=*/14,
                             /*box_span=*/0.0, /*view_penalty=*/0.0,
                             /*rot_diam=*/0.02, /*view_diam=*/0.10,
                             /*split_kappa=*/0.5, /*pre_vsplits=*/0, /*root_view_depth=*/0,
                             /*box_splits_since_view=*/2),
             "Fallback kappa: pacing box_splits_since_view=2 allows view split");

  // 5. Sidecar Splits File Round-Trip
  std::string test_splits_path = "test_sidecar.splits";
  std::unordered_map<int64_t, int> test_splits = {
      {1265125, 1},
      {1265469, 2},
      {9999999, 3},
  };
  CHECK_TRUE(SaveSplitsFile(test_splits_path, test_splits), "SaveSplitsFile succeeds");
  auto loaded_splits = LoadSplitsFile(test_splits_path);
  std::remove(test_splits_path.c_str());
  CHECK_TRUE(loaded_splits.size() == 3, "Loaded splits count matches");
  CHECK_TRUE(loaded_splits[1265125] == 1, "Loaded splits cell 1265125 == 1");
  CHECK_TRUE(loaded_splits[1265469] == 2, "Loaded splits cell 1265469 == 2");
  CHECK_TRUE(loaded_splits[9999999] == 3, "Loaded splits cell 9999999 == 3");
}

void TestCompletedFractionDifficultCell() {
  Print("Running TestCompletedFractionDifficultCell...\n");

  SearchManager mgr;
  mgr.auto_init_root = false;

  // Case 1: Full Chart (root_box_depth = 0, root_view_depth = 0)
  mgr.ResetState();
  mgr.root_box_depth = 0;
  mgr.root_view_depth = 0;
  CHECK_TRUE(mgr.CompletedFraction() == 1.0, "Full chart empty stack is 100% completed");

  SearchNode n_chart_root;
  n_chart_root.box_depth = 0;
  n_chart_root.view_depth = 0;
  mgr.stack.push_back(n_chart_root);
  CHECK_TRUE(mgr.CompletedFraction() == 0.0, "Full chart root on stack is 0% completed");

  // Split into 2 boxes: 1 remaining -> 50%
  mgr.stack.clear();
  SearchNode n_half = n_chart_root;
  n_half.box_depth = 1;
  mgr.stack.push_back(n_half);
  CHECK_TRUE(std::abs(mgr.CompletedFraction() - 0.5) < 1e-9, "Full chart 1 of 2 boxes remaining is 50% completed");

  // Case 2: Difficult Cell (box_depth = 32, view_depth = 8)
  mgr.ResetState();
  mgr.root_box_depth = 32;
  mgr.root_view_depth = 8;
  CHECK_TRUE(mgr.CompletedFraction() == 1.0, "Difficult cell empty stack is 100% completed");

  SearchNode n_diff_root;
  n_diff_root.box_depth = 32;
  n_diff_root.view_depth = 8;
  mgr.stack.push_back(n_diff_root);
  CHECK_TRUE(std::abs(mgr.CompletedFraction() - 0.0) < 1e-9,
             "Difficult cell root on stack is 0% completed (NOT 100%)");

  // Difficult cell with 2 box children: both on stack -> 0% completed
  mgr.stack.clear();
  SearchNode c0 = n_diff_root; c0.box_depth = 33;
  SearchNode c1 = n_diff_root; c1.box_depth = 33;
  mgr.stack.push_back(c0);
  mgr.stack.push_back(c1);
  CHECK_TRUE(std::abs(mgr.CompletedFraction() - 0.0) < 1e-9,
             "Difficult cell with both box children on stack is 0% completed");

  // One box child certified, one remaining -> 50% completed
  mgr.stack.pop_back();
  CHECK_TRUE(std::abs(mgr.CompletedFraction() - 0.5) < 1e-9,
             "Difficult cell with 1 of 2 box children remaining is 50% completed");

  // Pre-split cell into 4 view cones: all 4 on stack -> 0% completed
  mgr.stack.clear();
  for (int i = 0; i < 4; i++) {
    SearchNode vcone = n_diff_root;
    vcone.view_depth = 9; // view split adds 1
    mgr.stack.push_back(vcone);
  }
  CHECK_TRUE(std::abs(mgr.CompletedFraction() - 0.0) < 1e-9,
             "Difficult cell pre-split into 4 view cones on stack is 0% completed");

  // 3 view cones certified, 1 remaining -> 75% completed
  mgr.stack.resize(1);
  CHECK_TRUE(std::abs(mgr.CompletedFraction() - 0.75) < 1e-9,
             "Difficult cell with 1 of 4 view cones remaining is 75% completed");

  // Also verify current_batch is counted as pending volume
  mgr.stack.clear();
  mgr.current_batch.push_back(n_diff_root);
  CHECK_TRUE(std::abs(mgr.CompletedFraction() - 0.0) < 1e-9,
             "Difficult cell in active current_batch is 0% completed");
}

static void TestViewQuadtree() {
  Print(AYELLOW("  Testing ViewQuadtree serialization, manipulation, and path finding...\n"));

  // 1. Uniform quadtree generation
  auto u0 = ViewQuadtree::MakeUniform(0);
  CHECK_TRUE(!u0.is_split, "Depth 0 uniform quadtree is not split");
  CHECK_TRUE(u0.CountLeaves() == 1, "Depth 0 uniform quadtree has 1 leaf");
  CHECK_TRUE(u0.MaxDepth() == 0, "Depth 0 uniform quadtree has max depth 0");
  CHECK_TRUE(u0.ToString() == ".", "Depth 0 ToString is '.'");

  auto u1 = ViewQuadtree::MakeUniform(1);
  CHECK_TRUE(u1.is_split, "Depth 1 uniform quadtree is split");
  CHECK_TRUE(u1.CountLeaves() == 4, "Depth 1 uniform quadtree has 4 leaves");
  CHECK_TRUE(u1.MaxDepth() == 1, "Depth 1 uniform quadtree has max depth 1");
  CHECK_TRUE(u1.ToString() == "S . . . .", "Depth 1 ToString is 'S . . . .'");

  auto u2 = ViewQuadtree::MakeUniform(2);
  CHECK_TRUE(u2.CountLeaves() == 16, "Depth 2 uniform quadtree has 16 leaves");
  CHECK_TRUE(u2.MaxDepth() == 2, "Depth 2 uniform quadtree has max depth 2");

  // 2. FromString parsing & backward compatibility
  auto parsed_int = ViewQuadtree::FromString("3");
  CHECK_TRUE(parsed_int.has_value(), "Parsed integer '3' successfully");
  CHECK_TRUE(parsed_int->CountLeaves() == 64, "Integer '3' produces 64 leaves");
  CHECK_TRUE(parsed_int->MaxDepth() == 3, "Integer '3' produces max depth 3");

  auto parsed_tree = ViewQuadtree::FromString("S . . . S . . . .");
  CHECK_TRUE(parsed_tree.has_value(), "Parsed non-uniform quadtree successfully");
  CHECK_TRUE(parsed_tree->CountLeaves() == 7, "Non-uniform quadtree has 7 leaves (3 + 4)");
  CHECK_TRUE(parsed_tree->MaxDepth() == 2, "Non-uniform quadtree has max depth 2");
  CHECK_TRUE(parsed_tree->ToString() == "S . . . S . . . .", "Non-uniform round-trips ToString exactly");

  // 3. Dynamic path splitting
  ViewQuadtree dyn_tree = ViewQuadtree::MakeUniform(1); // 4 leaves
  std::vector<int> path1 = {3, 1}; // Split child 3, then sub-child 1
  dyn_tree.SplitPath(path1);
  // Root split into 4.
  // Child 3 split into 4.
  // Child 3.1 split into 4.
  // Leaves: 3 (from root children 0,1,2) + 3 (from child 3's 0,2,3) + 4 (from 3.1) = 10 leaves!
  CHECK_TRUE(dyn_tree.CountLeaves() == 10, "Splitting path [3, 1] results in 10 leaves");
  CHECK_TRUE(dyn_tree.MaxDepth() == 3, "Splitting path [3, 1] has max depth 3");

  // 4. Triangle path finding
  ProjectiveTriangle root_tri = {
    vec3{0.62290396341463428, 0.13881478658536586, 0.23828125},
    vec3{0.62585746951219523, 0.13586128048780488, 0.23828125},
    vec3{0.62681021341463428, 0.13881478658536586, 0.234375}
  };
  auto sub1 = root_tri.Subdivide();
  auto path_c2 = FindTrianglePath(root_tri, sub1[2]);
  CHECK_TRUE(path_c2.size() == 1 && path_c2[0] == 2, "Found path [2] for child 2");

  auto sub2 = sub1[3].Subdivide();
  auto path_c31 = FindTrianglePath(root_tri, sub2[1]);
  CHECK_TRUE(path_c31.size() == 2 && path_c31[0] == 3 && path_c31[1] == 1,
             "Found path [3, 1] for sub-triangle 3.1");

  // 5. Leaf extraction
  auto leaves = dyn_tree.GetLeaves(root_tri);
  CHECK_TRUE((int)leaves.size() == 10, "GetLeaves extracts exactly 10 leaf triangles");

  // 6. Sidecar file loading & saving
  std::string test_splits_file = "test_quadtree.splits";
  std::ofstream out(test_splits_file);
  out << "# id quadtree_or_vsplits\n";
  out << "1265114 5\n"; // legacy uniform integer
  out << "1265194 S . . . S . . . .\n"; // hierarchical quadtree
  out.close();

  auto loaded = LoadQuadtreeSplitsFile(test_splits_file);
  CHECK_TRUE(loaded.size() == 2, "Loaded 2 entries from test splits file");
  CHECK_TRUE(loaded[1265114].CountLeaves() == 1024, "Loaded uniform depth 5 -> 1024 leaves");
  CHECK_TRUE(loaded[1265194].CountLeaves() == 7, "Loaded non-uniform quadtree -> 7 leaves");

  std::string test_splits_out = "test_quadtree_out.splits";
  CHECK_TRUE(SaveQuadtreeSplitsFile(test_splits_out, loaded), "Saved quadtree splits file");
  auto reloaded = LoadQuadtreeSplitsFile(test_splits_out);
  CHECK_TRUE(reloaded[1265114].CountLeaves() == 1024, "Reloaded uniform tree preserves 1024 leaves");
  CHECK_TRUE(reloaded[1265194].CountLeaves() == 7, "Reloaded non-uniform tree preserves 7 leaves");

  unlink(test_splits_file.c_str());
  unlink(test_splits_out.c_str());
}

int main(int argc, char **argv) {
  Print(ACYAN("=== Running lib229 Bernstein & Splitting Unit Tests ===\n"));

  TestConstantPolynomial();
  TestLinearPolynomial();
  TestPureQuadratics();
  TestGeneralQuadraticCayleyScale();
  TestRandomizedPolynomialSuites();
  TestAnalyticalSplittingRule();
  TestCompletedFractionDifficultCell();
  TestViewQuadtree();

  if (g_failed_tests == 0) {
    Print(AGREEN("\nALL LIB229 UNIT TESTS PASSED WITH HIGH PRECISION!\n"));
    return 0;
  } else {
    Print(ARED("\nFAILED {} TESTS!\n"), g_failed_tests);
    return 1;
  }
}
