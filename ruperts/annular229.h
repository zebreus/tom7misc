// Standalone library for synthesizing Decomposed Annular / Flock Certificates
// for stubborn cells in Nopert #229 (e.g. canyon seam cells like 03121303200).
//
// When standard 4-axis caging fails due to the Obtuse Core Obstruction,
// this library decomposes orientation space into three regions:
//   1. Annular axis a0 covering the intermediate shell [r_min, r]
//      outside an exceptional cone C(u0, c_cone).
//   2. Complement cage (3 axes) covering adversary rotations outside C(u0, c_cone).
//   3. Core flock of K zero-defect axes covering the interior of C(u0, c_cone)
//      with positive margin >= c_core.

#ifndef _RUPERTS_ANNULAR229_H
#define _RUPERTS_ANNULAR229_H

#include <vector>
#include <string>
#include <array>
#include <memory>

#include "tubetree229.h"
#include "tube229.h"
#include "yocto-math.h"
#include "bignum/big.h"
#include "bignum/big-vec.h"

namespace annular229 {

using vec3 = yocto::vec<double, 3>;

// Configuration parameters for annular certificate search.
struct AnnularConfig {
  BigRat target_r{4, 100000};          // 4/100000 = 4e-5
  BigRat target_delta{26, 100000};     // 260/1000000 = 2.6e-4
  BigRat target_c_comp{3, 10000};      // 300/1000000 = 3e-4
  BigRat target_c_cone{25, 1000};      // 25/1000 = 0.025 (~75.5 deg cone)
  BigRat target_c_core{15, 10000};     // 15/10000 = 0.0015
  BigRat target_r_min{23, 10000};      // 23/10000 = 0.0023
  BigRat max_contact_defect{21, 100000}; // Max single-contact defect 2.1e-4
  BigRat max_D0{736, 10000000};        // Total weighted defect budget 7.36e-5

  int max_flock_size = 8;
  int cap_sample_count = 2000;
  int hull_axes_limit = 20000;
};

// Result of synthesis attempt.
struct AnnularSynthesisResult {
  bool success = false;
  tubetree229::DecomposedCertificate cert;
  std::string explanation;
};

// ============================================================================
// 1. Exact Rational Inequality Checks (matching Lean theorems)
// ============================================================================

// Evaluates annular dominance:
// ((1/2) * r^2 * B0 + D0)^2 <= r_min^2 * (1 - (1/4) * r^2) * ((c_cone - delta)^2 * B0^2)
bool CheckAnnularDominance(
    const BigRat &r, const BigRat &r_min,
    const BigRat &B0, const BigRat &D0,
    const BigRat &c_cone, const BigRat &delta);

// Evaluates core angle condition:
// r_min^2 * (1 + (c_core - delta)^2) <= 4 * (c_core - delta)^2
bool CheckCoreAngle(
    const BigRat &r_min, const BigRat &c_core, const BigRat &delta);

// Evaluates complement angle condition:
// r^2 * (1 + (c_comp - delta)^2) <= 4 * (c_comp - delta)^2
bool CheckComplementAngle(
    const BigRat &r, const BigRat &c_comp, const BigRat &delta);

// Evaluates whether barycentric coordinates of -u0 with respect to
// the complement axes are strictly positive with margin >= c_cone.
bool CheckBarycentricEnclosure(
    const vec3 &u0,
    const std::array<vec3, 3> &comp_axes,
    double c_cone,
    double *out_min_coord = nullptr);

// ============================================================================
// 2. Geometric Synthesis Routines
// ============================================================================

// Audits a candidate pool of zero-defect axes over triangle tri
// whose variation radius satisfies delta <= max_delta.
std::vector<tubetree229::AxisCertificate> CollectZeroDefectPool(
    const tubetree229::TriangleQ &tri,
    int max_axes = 20000,
    const BigRat &max_delta = BigRat(3, 10000));

// Given annular axis center u0, searches zero_defect_pool for 3 axes
// forming a complement cage that strictly encloses -u0 with margin >= c_comp.
bool FindComplementCage(
    const tubetree229::TriangleQ &tri,
    const vec3 &u0,
    const std::vector<tubetree229::AxisCertificate> &zero_defect_pool,
    double c_cone,
    std::array<tubetree229::AxisCertificate, 3> *out_cage);

// Solves spherical cap set cover using greedy approximation:
// Samples the spherical cap C(u0, c_cone) with N quasi-uniform points
// (Fibonacci spiral), and greedily chooses axes from pool that maximize
// uncovered sample points where dot(axis, pt) >= c_core.
bool FindCoreFlockGreedy(
    const tubetree229::TriangleQ &tri,
    const vec3 &u0,
    double c_cone,
    double c_core,
    const std::vector<tubetree229::AxisCertificate> &zero_defect_pool,
    std::vector<tubetree229::AxisCertificate> *out_flock,
    int max_k = 8,
    int num_samples = 2000);

// ============================================================================
// 3. End-to-End Annular Synthesizer
// ============================================================================

// Master entry point: synthesizes a complete decomposed annular certificate
// for triangle tri. Designed as a fallback when standard 4-axis caging fails.
AnnularSynthesisResult SynthesizeAnnularCertificate(
    const tubetree229::TriangleQ &tri,
    const std::vector<tubetree229::AxisCertificate> &seed_axes = {},
    const AnnularConfig &config = AnnularConfig{});

}  // namespace annular229

#endif  // _RUPERTS_ANNULAR229_H
