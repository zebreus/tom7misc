#include "annular229.h"

#include <iostream>
#include <cmath>
#include <algorithm>
#include <set>
#include <vector>

#include "tube229.h"
#include "tubetree229.h"
#include "yocto-math.h"
#include "base/logging.h"
#include "bignum/big-overloads.h"

namespace annular229 {

using namespace tubetree229;

bool CheckAnnularDominance(
    const BigRat &r, const BigRat &r_min,
    const BigRat &B0, const BigRat &D0,
    const BigRat &c_cone, const BigRat &delta) {
  BigRat diff = c_cone - delta;
  if (diff <= BigRat(0)) return false;

  BigRat lhs = BigRat(1, 2) * r * r * B0 + D0;
  lhs = lhs * lhs;

  BigRat rhs = r_min * r_min * (BigRat(1) - BigRat(1, 4) * r * r) * (diff * diff * B0 * B0);
  return lhs <= rhs;
}

bool CheckCoreAngle(
    const BigRat &r_min, const BigRat &c_core, const BigRat &delta) {
  BigRat diff = c_core - delta;
  if (diff <= BigRat(0)) return false;

  BigRat lhs = r_min * r_min * (BigRat(1) + diff * diff);
  BigRat rhs = BigRat(4) * diff * diff;
  return lhs <= rhs;
}

bool CheckComplementAngle(
    const BigRat &r, const BigRat &c_comp, const BigRat &delta) {
  BigRat diff = c_comp - delta;
  if (diff <= BigRat(0)) return false;

  BigRat lhs = r * r * (BigRat(1) + diff * diff);
  BigRat rhs = BigRat(4) * diff * diff;
  return lhs <= rhs;
}

bool CheckBarycentricEnclosure(
    const vec3 &u0,
    const std::array<vec3, 3> &comp_axes,
    double c_cone,
    double *out_min_coord) {
  // We want to express target = -u0 as a conical combination of comp_axes:
  // target = lambda0 * comp_axes[0] + lambda1 * comp_axes[1] + lambda2 * comp_axes[2]
  // Invert 3x3 matrix [v0 v1 v2]
  const vec3 &v0 = comp_axes[0];
  const vec3 &v1 = comp_axes[1];
  const vec3 &v2 = comp_axes[2];

  double det = yocto::dot(v0, yocto::cross(v1, v2));
  if (std::abs(det) < 1e-12) return false;

  vec3 target = -u0;
  double lam0 = yocto::dot(target, yocto::cross(v1, v2)) / det;
  double lam1 = yocto::dot(target, yocto::cross(v2, v0)) / det;
  double lam2 = yocto::dot(target, yocto::cross(v0, v1)) / det;

  double min_lam = std::min({lam0, lam1, lam2});
  if (out_min_coord) *out_min_coord = min_lam;

  // Strict enclosure requires all barycentric coordinates to be positive.
  // Moreover, the cage must cover outside the cone C(u0, c_cone).
  return min_lam > 1e-6;
}

std::vector<AxisCertificate> CollectZeroDefectPool(
    const TriangleQ &tri,
    int max_axes,
    const BigRat &max_delta) {
  auto hull_axes = Tube229::FindAllHullSupportAxes(tri, max_axes);
  std::vector<AxisCertificate> pool;
  pool.reserve(hull_axes.size());

  for (auto &cand : hull_axes) {
    if (cand.delta <= max_delta) {
      pool.push_back(cand.cert);
    }
  }
  return pool;
}

// Generate points on the spherical cap C(u0, c_cone) using Fibonacci spiral.
// The cap is defined by dot(axis, u0) >= c_cone where axis is unit length.
// Therefore, the angle alpha from normalize(u0) satisfies:
// cos(alpha) = c_cone / length(u0).
static std::vector<vec3> SampleSphericalCap(
    const vec3 &u0_unnormalized, double c_cone, int count) {
  std::vector<vec3> pts;
  pts.reserve(count);

  double norm_u0 = yocto::length(u0_unnormalized);
  if (norm_u0 < 1e-12) return pts;
  vec3 w = u0_unnormalized / norm_u0;
  double cos_theta_min = c_cone / norm_u0;
  if (cos_theta_min > 1.0) cos_theta_min = 1.0;
  if (cos_theta_min < -1.0) cos_theta_min = -1.0;

  vec3 a = (std::abs(w.z) < 0.9) ? vec3{0, 0, 1} : vec3{1, 0, 0};
  vec3 u = yocto::normalize(yocto::cross(w, a));
  vec3 v = yocto::cross(w, u);

  const double golden_ratio = (1.0 + std::sqrt(5.0)) / 2.0;

  for (int i = 0; i < count; i++) {
    double z = 1.0 - (1.0 - cos_theta_min) * (double(i) + 0.5) / double(count);
    double r_cyl = std::sqrt(std::max(0.0, 1.0 - z * z));
    double phi = 2.0 * M_PI * i / golden_ratio;

    double x = r_cyl * std::cos(phi);
    double y = r_cyl * std::sin(phi);

    vec3 pt = x * u + y * v + z * w;
    pts.push_back(yocto::normalize(pt));
  }
  return pts;
}

static vec3 GetAxisCenterDirection(const TriangleQ &tri, const AxisCertificate &ax) {
  BigVecQ3 center;
  BigRat delta;
  std::string err;
  AxisCertificate audited_cert;
  ContactInfo contacts[3] = {ax.contacts[0], ax.contacts[1], ax.contacts[2]};
  if (Tube229::AuditAxis(tri, contacts, &audited_cert, &center, &delta, &err, BigRat(1, 100))) {
    double B = audited_cert.B.ToDouble();
    return vec3{center.x.ToDouble() / B, center.y.ToDouble() / B, center.z.ToDouble() / B};
  }
  return {0, 0, 1};
}

bool FindCoreFlockGreedy(
    const TriangleQ &tri,
    const vec3 &u0,
    double c_cone,
    double c_core,
    const std::vector<AxisCertificate> &zero_defect_pool,
    std::vector<AxisCertificate> *out_flock,
    int max_k,
    int num_samples) {
  if (zero_defect_pool.empty()) return false;

  std::vector<vec3> cap_samples = SampleSphericalCap(u0, c_cone, num_samples);
  const size_t N = cap_samples.size();

  // Convert pool axes to normalized center directions
  std::vector<vec3> pool_dirs;
  pool_dirs.reserve(zero_defect_pool.size());
  for (const auto &ax : zero_defect_pool) {
    pool_dirs.push_back(GetAxisCenterDirection(tri, ax));
  }

  // Precompute coverage bitsets: which sample points does each candidate cover?
  std::vector<std::vector<int>> covered_by_axis(pool_dirs.size());
  for (size_t a = 0; a < pool_dirs.size(); a++) {
    for (size_t s = 0; s < N; s++) {
      if (yocto::dot(pool_dirs[a], cap_samples[s]) >= c_core) {
        covered_by_axis[a].push_back(s);
      }
    }
  }

  // Greedy set cover
  std::vector<bool> sample_covered(N, false);
  size_t remaining_uncovered = N;
  std::vector<int> selected_axes;

  std::cout << "  [FindCoreFlockGreedy] pool=" << zero_defect_pool.size()
            << ", cap=" << N << ", c_cone=" << c_cone << ", c_core=" << c_core
            << ", max_k=" << max_k << "\n";

  for (int iter = 0; iter < max_k && remaining_uncovered > 0; iter++) {
    int best_axis = -1;
    size_t best_new_covered = 0;

    for (size_t a = 0; a < pool_dirs.size(); a++) {
      size_t newly_covered = 0;
      for (int s_idx : covered_by_axis[a]) {
        if (!sample_covered[s_idx]) newly_covered++;
      }
      if (newly_covered > best_new_covered) {
        best_new_covered = newly_covered;
        best_axis = a;
      }
    }

    if (best_axis == -1 || best_new_covered == 0) {
      std::cout << "    iter " << iter << ": no more coverage progress! remaining = "
                << remaining_uncovered << "\n";
      for (size_t s = 0; s < N; s++) {
        if (!sample_covered[s]) {
          double dot_u0 = yocto::dot(u0, cap_samples[s]);
          double max_dot = -1.0;
          int best_a = -1;
          for (size_t a = 0; a < pool_dirs.size(); a++) {
            double d = yocto::dot(pool_dirs[a], cap_samples[s]);
            if (d > max_dot) {
              max_dot = d;
              best_a = a;
            }
          }
          std::cout << "      Uncovered sample " << s << ": dot(u0, s) = " << dot_u0
                    << ", max dot with pool = " << max_dot << " (c_core=" << c_core
                    << ", diff=" << (c_core - max_dot) << ", best_axis=" << best_a << ")\n";
        }
      }
      break;
    }

    selected_axes.push_back(best_axis);
    for (int s_idx : covered_by_axis[best_axis]) {
      if (!sample_covered[s_idx]) {
        sample_covered[s_idx] = true;
        remaining_uncovered--;
      }
    }
    std::cout << "    iter " << iter << ": added axis " << best_axis
              << ", newly covered: " << best_new_covered
              << ", remaining uncovered: " << remaining_uncovered << " / " << N << "\n";
  }

  if (remaining_uncovered == 0) {
    if (out_flock) {
      out_flock->clear();
      for (int a_idx : selected_axes) {
        out_flock->push_back(zero_defect_pool[a_idx]);
      }
    }
    return true;
  }
  return false;
}

bool FindComplementCage(
    const TriangleQ &tri,
    const vec3 &u0,
    const std::vector<AxisCertificate> &zero_defect_pool,
    double c_cone,
    std::array<AxisCertificate, 3> *out_cage) {
  if (zero_defect_pool.size() < 3) return false;

  std::vector<vec3> dirs;
  dirs.reserve(zero_defect_pool.size());
  for (const auto &ax : zero_defect_pool) {
    dirs.push_back(GetAxisCenterDirection(tri, ax));
  }

  // Find candidate axes that point roughly opposite to u0 (dot < 0)
  std::vector<int> opposing;
  for (size_t i = 0; i < dirs.size(); i++) {
    if (yocto::dot(u0, dirs[i]) < 0.0) opposing.push_back(i);
  }

  // Search triplets of opposing axes
  for (size_t i0 = 0; i0 < opposing.size(); i0++) {
    for (size_t i1 = i0 + 1; i1 < opposing.size(); i1++) {
      for (size_t i2 = i1 + 1; i2 < opposing.size(); i2++) {
        int a0 = opposing[i0], a1 = opposing[i1], a2 = opposing[i2];
        std::array<vec3, 3> triplet = {dirs[a0], dirs[a1], dirs[a2]};
        double min_bary = 0.0;
        if (CheckBarycentricEnclosure(u0, triplet, c_cone, &min_bary)) {
          if (out_cage) {
            (*out_cage)[0] = zero_defect_pool[a0];
            (*out_cage)[1] = zero_defect_pool[a1];
            (*out_cage)[2] = zero_defect_pool[a2];
          }
          return true;
        }
      }
    }
  }
  return false;
}

AnnularSynthesisResult SynthesizeAnnularCertificate(
    const TriangleQ &tri,
    const std::vector<AxisCertificate> &seed_axes,
    const AnnularConfig &config) {
  AnnularSynthesisResult res;

  // 1. Collect zero-defect candidate axes, seeded with any prior valid axes
  auto pool = seed_axes;
  auto hull_pool = CollectZeroDefectPool(tri, config.hull_axes_limit, config.target_delta);
  pool.insert(pool.end(), hull_pool.begin(), hull_pool.end());
  if (pool.empty()) {
    res.explanation = "Failed to collect any zero-defect axes under target delta.";
    return res;
  }

  // 2. Annular axis a0 (contacts on boundary)
  ContactInfo c0[3] = {
    {3, 2, 2, 1, 800, 2},
    {1, 4, 4, 8, 800, 4},
    {13, 14, 14, 15, 200, 14}
  };
  AxisCertificate ax0;
  BigVecQ3 ctr0;
  BigRat del0;
  std::string err;
  if (!Tube229::AuditAxis(tri, c0, &ax0, &ctr0, &del0, &err, config.max_contact_defect)) {
    res.explanation = "Failed to audit annular axis a0: " + err;
    return res;
  }

  // 3. Check rational bounds
  if (!CheckAnnularDominance(config.target_r, config.target_r_min, ax0.B, config.max_D0,
                             config.target_c_cone, config.target_delta)) {
    res.explanation = "Annular dominance inequality failed.";
    return res;
  }
  if (!CheckCoreAngle(config.target_r_min, config.target_c_core, config.target_delta)) {
    res.explanation = "Core angle inequality failed.";
    return res;
  }
  if (!CheckComplementAngle(config.target_r, config.target_c_comp, config.target_delta)) {
    res.explanation = "Complement angle inequality failed.";
    return res;
  }

  // 4. Find complement cage
  vec3 u0 = {ctr0.x.ToDouble() / ax0.B.ToDouble(),
             ctr0.y.ToDouble() / ax0.B.ToDouble(),
             ctr0.z.ToDouble() / ax0.B.ToDouble()};
  std::array<AxisCertificate, 3> comp_cage;
  if (!FindComplementCage(tri, u0, pool, config.target_c_cone.ToDouble(), &comp_cage)) {
    res.explanation = "Failed to find complement cage strictly enclosing -u0.";
    return res;
  }

  // 5. Find core flock covering cap C(u0, c_cone)
  std::vector<AxisCertificate> flock;
  if (!FindCoreFlockGreedy(tri, u0, config.target_c_cone.ToDouble(), config.target_c_core.ToDouble(),
                           pool, &flock, config.max_flock_size, config.cap_sample_count)) {
    res.explanation = "Failed to cover spherical cap with greedy core flock.";
    return res;
  }

  // Assemble certificate
  res.success = true;
  res.cert.annular_axis = ax0;
  res.cert.r = config.target_r;
  res.cert.r_min = config.target_r_min;
  res.cert.delta = config.target_delta;
  res.cert.c_comp = config.target_c_comp;
  res.cert.c_cone = config.target_c_cone;
  res.cert.c_core = config.target_c_core;
  res.cert.defect_D = config.max_D0;
  res.cert.complement_axes = {comp_cage[0], comp_cage[1], comp_cage[2]};
  res.cert.core_flock_axes = flock;
  res.explanation = "Successfully synthesized decomposed annular certificate with K=" +
                    std::to_string(flock.size()) + " core flock axes.";
  return res;
}

}  // namespace annular229
