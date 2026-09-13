// === Context & Problem Statement ===
// Steininger & Yurkevich (2025) proved the existence of a non-Rupert convex
// polyhedron: the "Noperthedron".
//
// Our objective:
// Produce a smaller Nopert (fewer vertices and faces) that can also be
// formally verified in Lean using the formalization of their theorem.
// Ideally it would be very close to nopert #214.
//
// "nopert_214" (20 vertices, 5-fold cyclic symmetry around the z-axis).
// Over billions of evaluations across global search methods, no macroscopic
// solution was ever found for #214. However, David's proof search did uncover
// a narrow ray of solutions (close to the identity), and we adapted the
// TILT_GRAD method to find more such solutions (Tom's original ALMOST_ID had
// a bug that prevented it from finding them).
// The solutions are very delicate: tilt angle θ ~ 9e-4 down to 5e-5 rad,
// clearance margin c ~ 1e-8 to 1.9e-7).
//
// Can we find a small perturbation of #214's geometry that resists solution
// entirely -- a proper, smaller Nopert?
//
// === Tom's Instructions & Notes ===
// 1. Stick with a symmetric polyhedron (5-fold cyclic symmetry, C_5).
// 2. Compute the half-spaces (convex hull) as the representation of #214,
//    choosing one of the "fans" of the five-fold cyclic symmetry.
// 3. Exactness: We can numerically approximate those rotated planes (and the
//    vertices of the convex hull), but since we're getting close to the limits
//    of floating point, represent the object in a way that is exact.
//    (We can say that the half-spaces have exact rational definitions given by
//    the actual floating point numbers.)
//    This makes verification easier later, because for example the shape will
//    be actually symmetric, not just approximately symmetric.
// 4. For now, we're trying to find a candidate that resists solving with the
//    numerical solvers.
//
// === Architectural Plan ===
//
// [A] Half-Space & Symmetry Representation:
//   - A fundamental fan / wedge of half-spaces: H_i = { x ∈ ℝ³ : n_i · x ≤ d_i }.
//   - Under 5-fold cyclic symmetry around z:
//       Polyhedron = ⋂_{k=0..4} ⋂_i R_z(2π k / 5) H_i.
//   - Defining the generator fan with rational coordinates / fixed floating
//     specifications guarantees exact symmetry without angular drift.
//
// [B] Pool-of-Solutions (Minimax / Cutting-Plane Rejection):
//   - We maintain a pool of known solution poses S = { (R_outer, R_inner, t) }.
//   - Since candidate polyhedra are strictly 5-fold symmetric around the z-axis,
//     symmetric copies of a pose produce identical 2D shadows and identical
//     clearance. Thus, we only store one pose per known solution (no pool inflation).
//   - For any perturbed candidate shape P:
//       1. Fast check: Evaluate GetClearance(P, R_outer, R_inner, t) for all poses in S.
//       2. If ANY pose in S yields c > 0, the candidate is immediately REJECTED.
//          (Cost: microseconds, filtering out ~99.9% of bad candidates).
//       3. Optional polish: Run 10-20 quick gradient steps from each pool pose to
//          ensure the local positive-clearance basin didn't simply drift.
//
// [C] Full Solver & Active-Set Learning:
//   - Only if a candidate survives the pool do we run heavier solvers:
//     TiltGradSolver (CPU) or OpenCL GPU tilt solver across S² view directions.
//   - If the solver discovers a new solution pose P_new:
//     Add P_new to pool S!
//     The pool grows strictly stronger, preventing future candidates from
//     re-opening that specific escape path.
//   - If the solver fails to find any solution after thorough sampling:
//     Candidate is flagged for dense verification / Lean export!
//
// [D] Bridge to Lean Formalization:
//   - Export candidate's exact half-spaces and dual vertices to Lean 4.
//   - Highly symmetric, 20-vertex geometry dramatically reduces branch-and-bound
//     complexity compared to the general Noperthedron. (On the other hand, since
//     it is not point-symmetric, we will need some new techniques.)

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <format>
#include <memory>
#include <mutex>
#include <numbers>
#include <optional>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#include "ansi.h"
#include "arcfour.h"
#include "base/logging.h"
#include "base/print.h"
#include "base/stringprintf.h"
#include "geom/hull-2d.h"
#include "geom/hull-3d.h"
#include "geom/polyhedra.h"
#include "opt/opt.h"
#include "randutil.h"
#include "ruperts-util.h"
#include "solutions.h"
#include "status-bar.h"
#include "threadutil.h"
#include "timer.h"
#include "yocto-math.h"

using vec2 = yocto::vec<double, 2>;
using vec3 = yocto::vec<double, 3>;
using frame2 = yocto::frame<double, 2>;
using frame3 = yocto::frame<double, 3>;
static inline quat4 RotationVectorToQuat(const vec3 &w) {
  const double theta = yocto::length(w);
  if (theta < 1e-12) {
    return quat4{0.0, 0.0, 0.0, 1.0};
  }
  const double half_theta = 0.5 * theta;
  const double s = std::sin(half_theta) / theta;
  return quat4{w.x * s, w.y * s, w.z * s, std::cos(half_theta)};
}

static inline frame3 RotationFrame(const vec3 &w) {
  double theta = yocto::length(w);
  if (theta < 1e-12) return frame3{};
  return yocto::rotation_frame(w / theta, theta);
}

static inline vec3 FaceNormal(const std::vector<vec3> &vertices,
                              const std::vector<int> &face) {
  CHECK(face.size() >= 3);
  const vec3 &v0 = vertices[face[0]];
  const vec3 &v1 = vertices[face[1]];
  const vec3 &v2 = vertices[face[2]];
  return yocto::normalize(yocto::cross(v1 - v0, v2 - v0));
}

static inline quat4 MakeTwoFacesParallelToZ(const std::vector<vec3> &vertices,
                                            const std::vector<int> &face1,
                                            const std::vector<int> &face2) {
  if (face1.size() < 3 || face2.size() < 3)
    return quat4{0.0, 0.0, 0.0, 1.0};

  const vec3 face1_normal = FaceNormal(vertices, face1);
  const vec3 face2_normal = FaceNormal(vertices, face2);

  vec3 x_axis = vec3{1.0, 0.0, 0.0};
  vec3 rot_axis = yocto::cross(face1_normal, x_axis);
  double rot1_angle = yocto::angle(face1_normal, x_axis);

  quat4 rot1 = QuatFromVec(yocto::rotation_quat(rot_axis, rot1_angle));

  const vec3 rot_face2_normal =
    yocto::transform_direction(yocto::rotation_frame(rot1), face2_normal);

  vec3 proj_normal = vec3{0.0, rot_face2_normal.y, rot_face2_normal.z};
  double rot2_angle = yocto::angle(proj_normal, vec3{0.0, 1.0, 0.0});
  quat4 rot2 = QuatFromVec(yocto::rotation_quat({1.0, 0.0, 0.0}, rot2_angle));

  return normalize(rot2 * rot1);
}

static inline quat4 AlignFaces(const std::vector<vec3> &vertices,
                               const std::vector<int> &face1,
                               const std::vector<int> &face2) {
  const quat4 parallel_inner_rot = MakeTwoFacesParallelToZ(
      vertices, face1, face2);

  const vec3 face1_normal = FaceNormal(vertices, face1);
  const vec3 xy_normal = yocto::transform_direction(
      yocto::rotation_frame(parallel_inner_rot), face1_normal);

  const double rot3_angle = std::atan2(xy_normal.x, xy_normal.y) +
    (std::numbers::pi * 0.5);
  const quat4 rot3 =
    QuatFromVec(yocto::rotation_quat({0.0, 0.0, 1.0}, rot3_angle));

  return normalize(rot3 * parallel_inner_rot);
}

// Represents a 5-fold cyclic symmetric polyhedron defined by a fundamental
// Represents a 5-fold cyclic symmetric polyhedron defined by:
// - axial caps (z-invariant, e.g. top and bottom planes)
// - generator half-spaces (rotated by 2π*k/5 around the z-axis)
struct SymmetricPolyHalfspaces {
  static constexpr int SYMMETRY_ORDER = 5;

  std::vector<HalfSpace> axial_caps;
  std::vector<HalfSpace> generators;

  // Generates all (axial_caps.size() + 5 * generators.size()) half-spaces.
  std::vector<HalfSpace> AllHalfspaces() const {
    std::vector<HalfSpace> all = axial_caps;
    all.reserve(axial_caps.size() + generators.size() * SYMMETRY_ORDER);
    for (int k = 0; k < SYMMETRY_ORDER; k++) {
      const double theta = (2.0 * std::numbers::pi * k) / SYMMETRY_ORDER;
      const double cos_t = std::cos(theta);
      const double sin_t = std::sin(theta);
      for (const auto &hs : generators) {
        vec3 rot_n(
            cos_t * hs.normal.x - sin_t * hs.normal.y,
            sin_t * hs.normal.x + cos_t * hs.normal.y,
            hs.normal.z);
        all.push_back(HalfSpace{.normal = rot_n, .d = hs.d});
      }
    }
    return all;
  }

  static SymmetricPolyHalfspaces FromPolyhedron(const Polyhedron &poly) {
    std::vector<HalfSpace> all = ExtractHalfSpacesFromHull(poly);
    SymmetricPolyHalfspaces sym;

    std::vector<HalfSpace> side_planes;
    for (const auto &h : all) {
      if (std::abs(std::abs(h.normal.z) - 1.0) < 1e-4) {
        sym.axial_caps.push_back(h);
      } else {
        side_planes.push_back(h);
      }
    }

    // Group side planes into rings by normal.z
    std::vector<std::vector<HalfSpace>> rings;
    for (const auto &h : side_planes) {
      bool found = false;
      for (auto &ring : rings) {
        if (std::abs(ring[0].normal.z - h.normal.z) < 1e-3 &&
            std::abs(ring[0].d - h.d) < 1e-3) {
          ring.push_back(h);
          found = true;
          break;
        }
      }
      if (!found) {
        rings.push_back({h});
      }
    }

    // Sort rings by normal.z ascending
    std::sort(rings.begin(), rings.end(),
              [](const std::vector<HalfSpace> &a, const std::vector<HalfSpace> &b) {
                return a[0].normal.z < b[0].normal.z;
              });

    const double sector = 2.0 * std::numbers::pi / SYMMETRY_ORDER;
    for (const auto &ring : rings) {
      HalfSpace best = ring[0];
      double best_rem = 100.0;
      for (const auto &h : ring) {
        double phi = std::atan2(h.normal.y, h.normal.x);
        if (phi < 0.0) phi += 2.0 * std::numbers::pi;
        double rem = std::fmod(phi, sector);
        if (rem < best_rem) {
          best_rem = rem;
          best = h;
        }
      }
      sym.generators.push_back(best);
    }

    return sym;
  }
};

// Pool of known solution poses for fast candidate rejection.
struct SolutionPool {
  struct Pose {
    int source_solution_id = 0;
    frame3 outer_frame;
    frame3 inner_frame;
    double original_clearance = 0.0;
  };

  std::vector<Pose> poses;

  // Load known solutions from the database (no need to store symmetric copies
  // since symmetric candidates have identical clearance under C_5 rotations).
  void Clear() { poses.clear(); }
  void LoadFromDB(SolutionDB *db, std::string_view poly_name) {
    std::vector<SolutionDB::Solution> sols = db->GetSolutionsFor(poly_name);
    poses.reserve(poses.size() + sols.size());
    for (const auto &sol : sols) {
      poses.push_back(Pose{
          .source_solution_id = sol.id,
          .outer_frame = sol.outer_frame,
          .inner_frame = sol.inner_frame,
          .original_clearance = sol.clearance,
      });
    }
  }

  // Evaluates the signed clearance of a polyhedron at a specific pose.
  // Uses MaximizeClearance2D over translation t.
  // clearance > 0: fits inside with margin.
  // clearance <= 0: minimum penetration depth.
  double SignedClearance(const Polyhedron &poly, const Pose &pose) const {
    const int num_v = poly.vertices.size();
    std::vector<vec2> outer_verts(num_v);
    std::vector<vec2> inner_verts(num_v);

    for (int i = 0; i < num_v; i++) {
      const vec3 &v = poly.vertices[i];
      outer_verts[i] = vec2{
          pose.outer_frame.x.x * v.x + pose.outer_frame.y.x * v.y + pose.outer_frame.z.x * v.z + pose.outer_frame.o.x,
          pose.outer_frame.x.y * v.x + pose.outer_frame.y.y * v.y + pose.outer_frame.z.y * v.z + pose.outer_frame.o.y,
      };
      inner_verts[i] = vec2{
          pose.inner_frame.x.x * v.x + pose.inner_frame.y.x * v.y + pose.inner_frame.z.x * v.z + pose.inner_frame.o.x,
          pose.inner_frame.x.y * v.x + pose.inner_frame.y.y * v.y + pose.inner_frame.z.y * v.z + pose.inner_frame.o.y,
      };
    }

    const std::vector<int> outer_hull = Hull2D::QuickHull(outer_verts);
    if (outer_hull.size() < 3) return -1.0;
    const auto edges = GetHullEdges(outer_verts, outer_hull);
    return MaximizeClearance2D(edges, inner_verts, vec2{0.0, 0.0}, 0.0001).clearance;
  }

  // Fast check: Returns the maximum clearance observed across all pool poses.
  // If this value is > 0, the polyhedron is definitely Rupert, and can be rejected.
  std::pair<double, int> MaxClearance(const Polyhedron &poly) const {
    double max_c = -std::numeric_limits<double>::infinity();
    int best_pose_idx = -1;

    for (int i = 0; i < (int)poses.size(); i++) {
      std::optional<double> c =
          GetClearance(poly, poses[i].outer_frame, poses[i].inner_frame);
      if (c.has_value()) {
        if (c.value() > max_c) {
          max_c = c.value();
          best_pose_idx = i;
        }
        if (c.value() > 0.0) {
          return {c.value(), i};
        }
      }
    }
    return {max_c, best_pose_idx};
  }

  // Attacks the candidate polyhedron locally near each of the 12 known solution poses
  // using gradient ascent on relative rotation w in R^3.
  // Returns:
  //   survived: true if NO pose was able to achieve clearance > 0
  //   max_achieved_clearance: highest clearance found by any local attack
  //   worst_pose_idx: which pose came closest or cracked it
  struct LocalAttackResult {
    bool survived = true;
    double max_achieved_clearance = -std::numeric_limits<double>::infinity();
    int worst_pose_idx = -1;
  };

  LocalAttackResult LocalTiltAttack(const Polyhedron &poly, int max_iters = 40) const {
    LocalAttackResult res;
    const int num_v = poly.vertices.size();
    if (num_v < 4) {
      res.survived = false;
      return res;
    }

    std::vector<vec2> outer_verts(num_v);
    std::vector<vec2> inner_verts(num_v);

    auto ProjectVerts = [&](const frame3 &f, std::vector<vec2> &out) {
      for (int i = 0; i < num_v; i++) {
        const vec3 &v = poly.vertices[i];
        out[i] = vec2{
            f.x.x * v.x + f.y.x * v.y + f.z.x * v.z + f.o.x,
            f.x.y * v.x + f.y.y * v.y + f.z.y * v.z + f.o.y,
        };
      }
    };

    for (int i = 0; i < (int)poses.size(); i++) {
      const frame3 &outer_frame = poses[i].outer_frame;
      ProjectVerts(outer_frame, outer_verts);
      const std::vector<int> outer_hull = Hull2D::QuickHull(outer_verts);
      if (outer_hull.size() < 3) continue;
      const auto edges = GetHullEdges(outer_verts, outer_hull);

      // Evaluate relative tilt around the inner frame:
      // candidate frame = RotationFrame(w_vec) * poses[i].inner_frame
      auto EvalW = [&](const vec3 &w_vec, vec2 initial_t) -> Clearance2D {
        const frame3 rf = RotationFrame(w_vec) * poses[i].inner_frame;
        ProjectVerts(rf, inner_verts);
        return MaximizeClearance2D(edges, inner_verts, initial_t, 0.0002);
      };

      // Search around w = 0 (the known solution pose)
      vec3 best_w = vec3{0.0, 0.0, 0.0};
      Clearance2D best_eval = EvalW(best_w, vec2{0.0, 0.0});

      // Gradient ascent on w
      static constexpr double EPS_DIFF = 1e-5;
      double step_size = 5e-4;

      for (int iter = 0; iter < max_iters; iter++) {
        if (best_eval.clearance > 0.0) {
          // Cracked!
          res.survived = false;
          res.max_achieved_clearance = best_eval.clearance;
          res.worst_pose_idx = i;
          return res;
        }

        vec3 grad{
            (EvalW(best_w + vec3{EPS_DIFF, 0.0, 0.0}, best_eval.translation).clearance -
             EvalW(best_w - vec3{EPS_DIFF, 0.0, 0.0}, best_eval.translation).clearance) /
                (2.0 * EPS_DIFF),
            (EvalW(best_w + vec3{0.0, EPS_DIFF, 0.0}, best_eval.translation).clearance -
             EvalW(best_w - vec3{0.0, EPS_DIFF, 0.0}, best_eval.translation).clearance) /
                (2.0 * EPS_DIFF),
            (EvalW(best_w + vec3{0.0, 0.0, EPS_DIFF}, best_eval.translation).clearance -
             EvalW(best_w - vec3{0.0, 0.0, EPS_DIFF}, best_eval.translation).clearance) /
                (2.0 * EPS_DIFF),
        };

        double grad_len = yocto::length(grad);
        if (grad_len < 1e-7) break;

        vec3 dir = grad / grad_len;
        bool improved = false;
        double s = step_size;
        for (int b = 0; b < 6; b++) {
          vec3 w_new = best_w + dir * s;
          Clearance2D res_new = EvalW(w_new, best_eval.translation);
          if (res_new.clearance > best_eval.clearance) {
            best_eval = res_new;
            best_w = w_new;
            improved = true;
            step_size = std::min(s * 1.2, 0.01);
            break;
          }
          s *= 0.5;
        }
        if (!improved) {
          step_size *= 0.5;
          if (step_size < 1e-6) break;
        }
      }

      if (best_eval.clearance > -5e-6) {
        // Nelder-Mead simplex polish on w around known pose
        double nm_step = 1e-5;
        vec3 p_nm[4] = {
            best_w,
            best_w + vec3{nm_step, 0.0, 0.0},
            best_w + vec3{0.0, nm_step, 0.0},
            best_w + vec3{0.0, 0.0, nm_step},
        };
        double val_nm[4];
        vec2 trans_nm[4];
        for (int k = 0; k < 4; k++) {
          auto res_k = EvalW(p_nm[k], best_eval.translation);
          val_nm[k] = res_k.clearance;
          trans_nm[k] = res_k.translation;
        }
        for (int iter = 0; iter < 30; iter++) {
          for (int a = 0; a < 3; a++) {
            for (int b = a + 1; b < 4; b++) {
              if (val_nm[b] > val_nm[a]) {
                std::swap(val_nm[a], val_nm[b]);
                std::swap(p_nm[a], p_nm[b]);
                std::swap(trans_nm[a], trans_nm[b]);
              }
            }
          }
          if (val_nm[0] > 0.0) {
            best_eval.clearance = val_nm[0];
            best_w = p_nm[0];
            best_eval.translation = trans_nm[0];
            break;
          }
          vec3 c = (p_nm[0] + p_nm[1] + p_nm[2]) / 3.0;
          vec3 xr = c + (c - p_nm[3]);
          auto rr = EvalW(xr, trans_nm[0]);
          if (rr.clearance > val_nm[0]) {
            vec3 xe = c + (xr - c) * 2.0;
            auto re = EvalW(xe, rr.translation);
            if (re.clearance > rr.clearance) {
              p_nm[3] = xe; val_nm[3] = re.clearance; trans_nm[3] = re.translation;
            } else {
              p_nm[3] = xr; val_nm[3] = rr.clearance; trans_nm[3] = rr.translation;
            }
          } else if (rr.clearance > val_nm[2]) {
            p_nm[3] = xr; val_nm[3] = rr.clearance; trans_nm[3] = rr.translation;
          } else {
            vec3 xc = c + (p_nm[3] - c) * 0.5;
            auto rc = EvalW(xc, trans_nm[0]);
            if (rc.clearance > val_nm[3]) {
              p_nm[3] = xc; val_nm[3] = rc.clearance; trans_nm[3] = rc.translation;
            } else {
              for (int k = 1; k < 4; k++) {
                p_nm[k] = p_nm[0] + (p_nm[k] - p_nm[0]) * 0.5;
                auto rk = EvalW(p_nm[k], trans_nm[0]);
                val_nm[k] = rk.clearance;
                trans_nm[k] = rk.translation;
              }
            }
          }
        }
        if (val_nm[0] > best_eval.clearance) {
          best_eval.clearance = val_nm[0];
          best_w = p_nm[0];
          best_eval.translation = trans_nm[0];
        }
      }

      // Outer pose optimization (phi) around known outer pose
      if (best_eval.clearance > -5e-5) {
        vec3 phi = vec3{0.0, 0.0, 0.0};
        double outer_step = 2e-4;
        auto EvalOuterInner = [&](const vec3 &p_outer, const vec3 &w_inner,
                                  vec2 t_hint) -> Clearance2D {
          const frame3 fo = yocto::rotation_frame(RotationVectorToQuat(p_outer)) * poses[i].outer_frame;
          ProjectVerts(fo, outer_verts);
          const std::vector<int> ho = Hull2D::QuickHull(outer_verts);
          if (ho.size() < 3) return Clearance2D{.clearance = -1.0, .translation = vec2{0.0, 0.0}};
          const auto eo = GetHullEdges(outer_verts, ho);
          const frame3 fi = yocto::rotation_frame(RotationVectorToQuat(w_inner)) * poses[i].inner_frame;
          ProjectVerts(fi, inner_verts);
          return MaximizeClearance2D(eo, inner_verts, t_hint, 0.0002);
        };

        for (int p_iter = 0; p_iter < 8; p_iter++) {
          if (best_eval.clearance > 1e-9) break;
          const vec3 grad_phi{
              (EvalOuterInner(phi + vec3{EPS_DIFF, 0.0, 0.0}, best_w, best_eval.translation).clearance -
               EvalOuterInner(phi - vec3{EPS_DIFF, 0.0, 0.0}, best_w, best_eval.translation).clearance) / (2.0 * EPS_DIFF),
              (EvalOuterInner(phi + vec3{0.0, EPS_DIFF, 0.0}, best_w, best_eval.translation).clearance -
               EvalOuterInner(phi - vec3{0.0, EPS_DIFF, 0.0}, best_w, best_eval.translation).clearance) / (2.0 * EPS_DIFF),
              (EvalOuterInner(phi + vec3{0.0, 0.0, EPS_DIFF}, best_w, best_eval.translation).clearance -
               EvalOuterInner(phi - vec3{0.0, 0.0, EPS_DIFF}, best_w, best_eval.translation).clearance) / (2.0 * EPS_DIFF),
          };
          double gp_norm = yocto::length(grad_phi);
          if (gp_norm < 1e-12) break;
          vec3 u_gp = grad_phi / gp_norm;
          double a = outer_step;
          for (int ls = 0; ls < 5; ls++) {
            vec3 p_cand = phi + u_gp * a;
            auto res = EvalOuterInner(p_cand, best_w, best_eval.translation);
            if (res.clearance > best_eval.clearance) {
              best_eval = res;
              phi = p_cand;
              outer_step = std::min(0.005, a * 1.4);
              break;
            }
            a *= 0.5;
          }
        }
      }

      if (best_eval.clearance > res.max_achieved_clearance) {
        res.max_achieved_clearance = best_eval.clearance;
        res.worst_pose_idx = i;
      }
      if (best_eval.clearance > 0.0) {
        res.survived = false;
        return res;
      }
    }

    res.survived = (res.max_achieved_clearance <= 0.0);
    return res;
  }
};

// Reconstruct a convex polyhedron from its bounding half-spaces via polar duality.
// For half-spaces n_i · x <= d_i (with d_i > 0):
// 1. Dual points are w_i = n_i / d_i.
// 2. Compute 3D triangular hull of dual points.
// 3. Each dual facet plane N · w = c corresponds to a primal vertex v = N / c.
// 4. Deduplicate near-identical vertices.
static std::optional<Polyhedron> HalfspacesToPolyhedron(
    const std::vector<HalfSpace> &planes, std::string_view name = "perturbed") {
  if (planes.size() < 4) return std::nullopt;

  std::vector<vec3> dual_verts;
  dual_verts.reserve(planes.size());

  for (const auto &hs : planes) {
    if (hs.d <= 1e-9) {
      return std::nullopt;
    }
    dual_verts.push_back(hs.normal / hs.d);
  }

  auto triangles = Hull3D::HullFaces(dual_verts);
  if (triangles.empty()) return std::nullopt;

  std::vector<vec3> primal_verts;
  for (const auto &[i, j, k] : triangles) {
    const vec3 &w0 = dual_verts[i];
    const vec3 &w1 = dual_verts[j];
    const vec3 &w2 = dual_verts[k];

    vec3 cross_prod = yocto::cross(w1 - w0, w2 - w0);
    double c = yocto::dot(cross_prod, w0);
    if (std::abs(c) < 1e-12) continue;

    // Primal vertex satisfies v · w = 1
    vec3 v = cross_prod / c;

    // Deduplicate near-identical vertices resulting from triangulating
    // non-simplicial dual faces (true vertices in #214 are separated by > 0.2).
    bool dup = false;
    for (const vec3 &existing : primal_verts) {
      if (yocto::length(existing - v) < 1e-4) {
        dup = true;
        break;
      }
    }
    if (!dup) {
      primal_verts.push_back(v);
    }
  }

  if (primal_verts.size() < 4) return std::nullopt;

  Polyhedron poly;
  poly.name = std::string(name);
  poly.vertices = std::move(primal_verts);

  auto primal_triangles = Hull3D::HullFaces(poly.vertices);
  if (!primal_triangles.empty()) {
    std::vector<std::vector<int>> face_indices;
    face_indices.reserve(primal_triangles.size());
    for (const auto &[i, j, k] : primal_triangles) {
      face_indices.push_back({i, j, k});
    }
    poly.faces = std::shared_ptr<const Faces>(Faces::Create(poly.vertices, face_indices));
  }

  return poly;
}

struct TimedAttackResult {
  bool cracked = false;
  double best_clearance = -std::numeric_limits<double>::infinity();
  int64_t total_trials = 0;
  double elapsed_seconds = 0.0;
  frame3 best_outer;
  frame3 best_inner;
};

// Full multi-threaded CPU TiltGrad solver attack running for time_limit_seconds across num_threads.
static TimedAttackResult RunParallelTimedTiltGradAttack(
    const Polyhedron &poly,
    double time_limit_seconds,
    int num_threads,
    std::string_view label,
    StatusBar *status) {
  TimedAttackResult result;
  const int num_v = poly.vertices.size();
  if (num_v < 4) return result;

  std::atomic<bool> should_die{false};
  std::atomic<int64_t> total_trials{0};
  std::mutex result_mutex;
  Timer run_timer;

  static constexpr double TEST_SCALES[] = {0.0008, 0.002, 0.006, 0.015, 0.04};
  static const auto TEST_DIRS = [] {
    std::vector<vec3> dirs;
    for (int dx = -1; dx <= 1; dx++) {
      for (int dy = -1; dy <= 1; dy++) {
        for (int dz = -1; dz <= 1; dz++) {
          if (dx == 0 && dy == 0 && dz == 0) continue;
          dirs.push_back(yocto::normalize(vec3{(double)dx, (double)dy, (double)dz}));
        }
      }
    }
    return dirs;
  }();

  ParallelFan(num_threads, [&](int thread_idx) {
    ArcFour rc(StringPrintf("repair214.%d.%lld", thread_idx, (long long)time(nullptr)));

    std::vector<vec2> outer_verts(num_v);
    std::vector<vec2> inner_verts(num_v);
    std::vector<vec2> temp_verts(num_v);

    auto ProjectVertices = [&](const frame3 &f, std::vector<vec2> &out) {
      for (int i = 0; i < num_v; i++) {
        const vec3 &v = poly.vertices[i];
        out[i] = vec2{
            f.x.x * v.x + f.y.x * v.y + f.z.x * v.z + f.o.x,
            f.x.y * v.x + f.y.y * v.y + f.z.y * v.z + f.o.y,
        };
      }
    };

    auto MakeInnerFrame = [&](const vec3 &w_sol, const vec2 &t, const quat4 &base_qo) {
      const quat4 delta_q = RotationVectorToQuat(w_sol);
      const quat4 q_in = yocto::normalize(delta_q * base_qo);
      const frame3 rot_f = yocto::rotation_frame(q_in);
      return yocto::translation_frame(vec3{t.x, t.y, 0.0}) * rot_f;
    };

    double last_status_update = 0.0;
    double last_log_update = 0.0;

    while (!should_die.load(std::memory_order_relaxed)) {
      double elapsed = run_timer.Seconds();
      if (elapsed >= time_limit_seconds) {
        should_die.store(true, std::memory_order_relaxed);
        break;
      }

      // Thread 0 periodically updates status bar and logs
      if (thread_idx == 0) {
        if (elapsed - last_status_update >= 0.25) {
          last_status_update = elapsed;
          int64_t trials = total_trials.load(std::memory_order_relaxed);
          double rate = trials / std::max(0.001, elapsed);
          double cur_best;
          {
            std::lock_guard<std::mutex> lk(result_mutex);
            cur_best = result.best_clearance;
          }
          if (status != nullptr) {
            status->Progress((int64_t)elapsed, (int64_t)time_limit_seconds,
                             "{}: {} trials ({:.0f}/s) best c: {:+.3e}",
                             label, trials, rate, cur_best);
          }
        }
        if (elapsed - last_log_update >= 30.0) {
          last_log_update = elapsed;
          int64_t trials = total_trials.load(std::memory_order_relaxed);
          double rate = trials / std::max(0.001, elapsed);
          double cur_best;
          {
            std::lock_guard<std::mutex> lk(result_mutex);
            cur_best = result.best_clearance;
          }
          if (status != nullptr) {
            status->Printf("  [%s] %5.1f%% [%s/%s]: %lld trials (%.0f/s) best c: %+.6e\n",
                           std::string(label).c_str(),
                           100.0 * elapsed / time_limit_seconds,
                           ANSI::Time(elapsed).c_str(),
                           ANSI::Time(time_limit_seconds).c_str(),
                           (long long)trials, rate, cur_best);
          }
        }
      }

      quat4 q_outer;
      const int mode = RandTo(&rc, 10);
      if (mode < 5) {
        // 50%: Local shadow area maximization
        const quat4 initial_rot = RandomQuaternion(&rc);
        auto AreaLoss = [&](const std::array<double, 4> &args) {
          const auto &[o0, o1, o2, o3] = args;
          quat4 rot = yocto::normalize(quat4{
              .x = initial_rot.x + o0,
              .y = initial_rot.y + o1,
              .z = initial_rot.z + o2,
              .w = initial_rot.w + o3,
          });
          frame3 f = yocto::rotation_frame(rot);
          ProjectVertices(f, temp_verts);
          std::vector<int> h = Hull2D::QuickHull(temp_verts);
          return -AreaOfHull(temp_verts, h);
        };
        const std::array<double, 4> area_lb = {-0.1, -0.1, -0.1, -0.1};
        const std::array<double, 4> area_ub = {+0.1, +0.1, +0.1, +0.1};
        const auto [area_args, area_err] =
            Opt::Minimize<4>(AreaLoss, area_lb, area_ub, 300, 1, 1, Rand32(&rc));
        q_outer = yocto::normalize(quat4{
            .x = initial_rot.x + area_args[0],
            .y = initial_rot.y + area_args[1],
            .z = initial_rot.z + area_args[2],
            .w = initial_rot.w + area_args[3],
        });
      } else if (mode < 8 && poly.faces != nullptr && poly.faces->v.size() >= 2) {
        // 30%: Two faces aligned with z / y
        const auto &[face1, face2] = TwoNonParallelFaces(&rc, poly);
        q_outer = AlignFaces(poly.vertices, poly.faces->v[face1], poly.faces->v[face2]);
      } else {
        // 20%: Uniform random orientation
        q_outer = RandomQuaternion(&rc);
      }

      frame3 outer_frame = yocto::rotation_frame(q_outer);
      ProjectVertices(outer_frame, outer_verts);
      if (AllZero(outer_verts)) continue;
      const std::vector<int> outer_hull = Hull2D::QuickHull(outer_verts);
      if (outer_hull.size() < 3) continue;
      const auto edges = GetHullEdges(outer_verts, outer_hull);

      auto EvalW = [&](const vec3 &w_in, vec2 initial_t, double initial_step = 0.0005) -> Clearance2D {
        const quat4 delta_q = RotationVectorToQuat(w_in);
        const quat4 q_inner = yocto::normalize(delta_q * q_outer);
        const frame3 rot_frame = yocto::rotation_frame(q_inner);
        ProjectVertices(rot_frame, inner_verts);
        return MaximizeClearance2D(edges, inner_verts, initial_t, initial_step);
      };

      vec3 best_w = TEST_DIRS[0] * TEST_SCALES[0];
      double best_clearance = -std::numeric_limits<double>::infinity();
      vec2 best_trans = vec2{0.0, 0.0};

      for (double r : TEST_SCALES) {
        for (const vec3 &dir : TEST_DIRS) {
          const vec3 w_cand = dir * r;
          const auto res = EvalW(w_cand, vec2{0.0, 0.0});
          if (res.clearance > best_clearance) {
            best_clearance = res.clearance;
            best_w = w_cand;
            best_trans = res.translation;
          }
        }
      }

      vec3 w = best_w;
      double step_size = 5e-4;
      static constexpr double EPS_DIFF = 1e-5;

      for (int iter = 0; iter < 40; iter++) {
        if (best_clearance > 1e-9) break;

        vec3 grad{
            (EvalW(w + vec3{EPS_DIFF, 0.0, 0.0}, best_trans, 1e-5).clearance -
             EvalW(w - vec3{EPS_DIFF, 0.0, 0.0}, best_trans, 1e-5).clearance) /
                (2.0 * EPS_DIFF),
            (EvalW(w + vec3{0.0, EPS_DIFF, 0.0}, best_trans, 1e-5).clearance -
             EvalW(w - vec3{0.0, EPS_DIFF, 0.0}, best_trans, 1e-5).clearance) /
                (2.0 * EPS_DIFF),
            (EvalW(w + vec3{0.0, 0.0, EPS_DIFF}, best_trans, 1e-5).clearance -
             EvalW(w - vec3{0.0, 0.0, EPS_DIFF}, best_trans, 1e-5).clearance) /
                (2.0 * EPS_DIFF),
        };

        const double w_len = yocto::length(w);
        if (w_len > 1e-8) {
          const vec3 u_w = w / w_len;
          const double radial = yocto::dot(grad, u_w);
          if (best_clearance <= 0.0 && radial < 0.0) {
            grad -= u_w * radial;
          }
        }

        const double g_norm = yocto::length(grad);
        if (g_norm < 1e-12) break;
        const vec3 u_g = grad / g_norm;

        double alpha = step_size;
        bool improved = false;
        for (int ls = 0; ls < 8; ls++) {
          const vec3 w_cand = w + u_g * alpha;
          const auto res = EvalW(w_cand, best_trans, 1e-5);
          if (res.clearance > best_clearance) {
            best_clearance = res.clearance;
            best_w = w_cand;
            w = w_cand;
            best_trans = res.translation;
            step_size = std::min(0.01, alpha * 1.4);
            improved = true;
            break;
          }
          alpha *= 0.5;
        }
        if (!improved) {
          step_size *= 0.5;
          if (step_size < 1e-8) break;
        }
      }

      // Step 3: Nelder-Mead Polish on w when clearance is promising.
      if (best_clearance > -5e-6 && yocto::length(best_w) > 1e-8) {
        double nm_step = 1e-5;
        vec3 p_nm[4] = {
            best_w,
            best_w + vec3{nm_step, 0.0, 0.0},
            best_w + vec3{0.0, nm_step, 0.0},
            best_w + vec3{0.0, 0.0, nm_step},
        };
        double val_nm[4];
        vec2 trans_nm[4];
        for (int i = 0; i < 4; i++) {
          auto res = EvalW(p_nm[i], best_trans, 1e-5);
          val_nm[i] = res.clearance;
          trans_nm[i] = res.translation;
        }

        for (int iter = 0; iter < 50; iter++) {
          for (int i = 0; i < 3; i++) {
            for (int j = i + 1; j < 4; j++) {
              if (val_nm[j] > val_nm[i]) {
                std::swap(val_nm[i], val_nm[j]);
                std::swap(p_nm[i], p_nm[j]);
                std::swap(trans_nm[i], trans_nm[j]);
              }
            }
          }

          if (val_nm[0] > 0.0) {
            best_w = p_nm[0];
            best_trans = trans_nm[0];
            best_clearance = val_nm[0];
            break;
          }

          vec3 c = (p_nm[0] + p_nm[1] + p_nm[2]) / 3.0;
          vec3 xr = c + (c - p_nm[3]);
          auto rr = EvalW(xr, trans_nm[0], 1e-5);
          if (rr.clearance > val_nm[0]) {
            vec3 xe = c + (xr - c) * 2.0;
            auto re = EvalW(xe, rr.translation, 1e-5);
            if (re.clearance > rr.clearance) {
              p_nm[3] = xe; val_nm[3] = re.clearance; trans_nm[3] = re.translation;
            } else {
              p_nm[3] = xr; val_nm[3] = rr.clearance; trans_nm[3] = rr.translation;
            }
          } else if (rr.clearance > val_nm[2]) {
            p_nm[3] = xr; val_nm[3] = rr.clearance; trans_nm[3] = rr.translation;
          } else {
            vec3 xc = c + (p_nm[3] - c) * 0.5;
            auto rc = EvalW(xc, trans_nm[0], 1e-5);
            if (rc.clearance > val_nm[3]) {
              p_nm[3] = xc; val_nm[3] = rc.clearance; trans_nm[3] = rc.translation;
            } else {
              for (int i = 1; i < 4; i++) {
                p_nm[i] = p_nm[0] + (p_nm[i] - p_nm[0]) * 0.5;
                auto ri = EvalW(p_nm[i], trans_nm[0], 1e-5);
                val_nm[i] = ri.clearance;
                trans_nm[i] = ri.translation;
              }
            }
          }
        }
        if (val_nm[0] > best_clearance) {
          best_clearance = val_nm[0];
          best_w = p_nm[0];
          best_trans = trans_nm[0];
        }
      }

      if (best_clearance > 0.0 && yocto::length(best_w) > 1e-8) {
        const frame3 inner_f = MakeInnerFrame(best_w, best_trans, q_outer);
        const auto cl = GetClearance(poly, outer_frame, inner_f);
        if (cl.has_value() && cl.value() > 0.0) {
          std::lock_guard<std::mutex> lk(result_mutex);
          result.cracked = true;
          result.best_clearance = cl.value();
          result.best_outer = outer_frame;
          result.best_inner = inner_f;
          should_die.store(true, std::memory_order_relaxed);
          break;
        }
      }

      // Step 4: Outer pose optimization (phi)
      vec3 phi = vec3{0.0, 0.0, 0.0};
      if (best_clearance > -5e-5 && yocto::length(best_w) > 1e-8) {
        double outer_step = 2e-4;
        auto EvalOuterInner = [&](const vec3 &p_outer, const vec3 &w_inner,
                                  vec2 t_hint, double initial_step = 1e-5) -> Clearance2D {
          const quat4 d_qo = RotationVectorToQuat(p_outer);
          const quat4 qo = yocto::normalize(d_qo * q_outer);
          const frame3 fo = yocto::rotation_frame(qo);
          ProjectVertices(fo, outer_verts);
          const std::vector<int> ho = Hull2D::QuickHull(outer_verts);
          if (ho.size() < 3) return Clearance2D{.clearance = -1.0, .translation = vec2{0.0, 0.0}};
          const auto eo = GetHullEdges(outer_verts, ho);

          const quat4 d_qi = RotationVectorToQuat(w_inner);
          const quat4 qi = yocto::normalize(d_qi * qo);
          const frame3 fi = yocto::rotation_frame(qi);
          ProjectVertices(fi, inner_verts);
          return MaximizeClearance2D(eo, inner_verts, t_hint, initial_step);
        };

        for (int p_iter = 0; p_iter < 12; p_iter++) {
          if (best_clearance > 1e-9) break;

          const vec3 grad_phi{
              (EvalOuterInner(phi + vec3{EPS_DIFF, 0.0, 0.0}, best_w, best_trans).clearance -
               EvalOuterInner(phi - vec3{EPS_DIFF, 0.0, 0.0}, best_w, best_trans).clearance) / (2.0 * EPS_DIFF),
              (EvalOuterInner(phi + vec3{0.0, EPS_DIFF, 0.0}, best_w, best_trans).clearance -
               EvalOuterInner(phi - vec3{0.0, EPS_DIFF, 0.0}, best_w, best_trans).clearance) / (2.0 * EPS_DIFF),
              (EvalOuterInner(phi + vec3{0.0, 0.0, EPS_DIFF}, best_w, best_trans).clearance -
               EvalOuterInner(phi - vec3{0.0, 0.0, EPS_DIFF}, best_w, best_trans).clearance) / (2.0 * EPS_DIFF),
          };

          const double p_norm = yocto::length(grad_phi);
          if (p_norm < 1e-12) break;
          const vec3 u_p = grad_phi / p_norm;

          double alpha = outer_step;
          for (int ls = 0; ls < 6; ls++) {
            const vec3 phi_cand = phi + u_p * alpha;
            const auto res = EvalOuterInner(phi_cand, best_w, best_trans);
            if (res.clearance > best_clearance) {
              best_clearance = res.clearance;
              phi = phi_cand;
              best_trans = res.translation;
              outer_step = std::min(0.005, alpha * 1.4);
              break;
            }
            alpha *= 0.5;
          }
        }

        if (best_clearance > 0.0) {
          const quat4 final_qo = yocto::normalize(RotationVectorToQuat(phi) * q_outer);
          const frame3 final_fo = yocto::rotation_frame(final_qo);
          const frame3 final_fi = MakeInnerFrame(best_w, best_trans, final_qo);
          const auto cl = GetClearance(poly, final_fo, final_fi);
          if (cl.has_value() && cl.value() > 0.0) {
            std::lock_guard<std::mutex> lk(result_mutex);
            result.cracked = true;
            result.best_clearance = cl.value();
            result.best_outer = final_fo;
            result.best_inner = final_fi;
            should_die.store(true, std::memory_order_relaxed);
            break;
          }
        }
      }

      total_trials.fetch_add(1, std::memory_order_relaxed);

      if (best_clearance > -0.05) {
        std::lock_guard<std::mutex> lk(result_mutex);
        if (best_clearance > result.best_clearance) {
          result.best_clearance = best_clearance;
        }
      }
    }
  });

  result.elapsed_seconds = run_timer.Seconds();
  result.total_trials = total_trials.load();
  return result;
}

struct Options {
  std::string candidate = "all";
  double seconds_per_candidate = 1800.0;
  int num_threads = 8;
  bool quick = false;
  bool pool_only = false;
  bool insert_db = false;
};

static void Repair214(const Options &options, StatusBar *status) {
  Print(AGREEN("=== repair214: Nopert #214 Perturbation & Defense Lab ===") "\n\n");

  SolutionDB db;
  Polyhedron poly214 = db.AnyPolyhedronByName("nopert_214");
  Print("Loaded {}: {} vertices. Volume = {:.10f}\n",
        poly214.name, poly214.vertices.size(), Volume(poly214));
  for (size_t vi = 0; vi < poly214.vertices.size(); vi++) {
    Print("  orig v[{:2d}] = ({:+.8f}, {:+.8f}, {:+.8f})\n",
          vi, poly214.vertices[vi].x, poly214.vertices[vi].y, poly214.vertices[vi].z);
  }

  if (poly214.faces != nullptr) {
    Print("Faces of nopert_214 (total {}):\n", poly214.faces->v.size());
    for (size_t fi = 0; fi < poly214.faces->v.size(); fi++) {
      const auto &fv = poly214.faces->v[fi];
      std::string s;
      for (int vi : fv) s += std::format(" {}", vi);
      Print("  Face {:2d} (size {}):{}\n", fi, fv.size(), s);
    }
  }

  SolutionPool pool;
  pool.LoadFromDB(&db, "nopert_214");
  pool.LoadFromDB(&db, "nopert_227");
  pool.LoadFromDB(&db, "nopert_228");
  Print("Loaded {} solution poses into rejection pool (from 214, 227, 228).\n", pool.poses.size());

  // Test root coplanarity formula
  vec3 r0 = poly214.vertices[0];
  vec3 r1 = poly214.vertices[5];
  vec3 r2 = poly214.vertices[10];
  vec3 r3 = poly214.vertices[15];

  double theta = -2.0 * std::numbers::pi / 5.0;
  double cos_t = std::cos(theta), sin_t = std::sin(theta);
  vec3 r1_rot(cos_t * r1.x - sin_t * r1.y, sin_t * r1.x + cos_t * r1.y, r1.z);

  double dx_rot = r1_rot.x - r1.x;
  double dy_rot = r1_rot.y - r1.y;

  double C1 = (r2.x - r0.x) * dy_rot - (r2.y - r0.y) * dx_rot;
  double C2 = (r2.z - r0.z) * ((r1.x - r0.x) * dy_rot - (r1.y - r0.y) * dx_rot);
  double z1_derived = r0.z + C2 / C1;
  Print("Root Quad Formula Check:\n"
        "  Actual r1.z   = {:.16f}\n"
        "  Derived z1    = {:.16f}\n"
        "  Difference    = {:+.16e}\n"
        "  C1 denom      = {:.10f}\n",
        r1.z, z1_derived, z1_derived - r1.z, C1);

  vec3 r1_exact = r1;
  r1_exact.z = z1_derived;
  std::vector<vec3> test_verts;
  for (vec3 r : {r0, r1_exact, r2, r3}) {
    for (int k = 0; k < 5; k++) {
      double angle = (2.0 * std::numbers::pi * k) / 5.0;
      double c = std::cos(angle), s = std::sin(angle);
      test_verts.emplace_back(c * r.x - s * r.y, s * r.x + c * r.y, r.z);
    }
  }
  auto test_poly = PolyhedronFromVertices(test_verts, "test_root");
  Print(AGREEN("Constructed from exact root: {} vertices, {} faces!\n"),
        test_poly->vertices.size(),
        test_poly->faces != nullptr ? (int)test_poly->faces->v.size() : 0);

  auto [exact_max_c, exact_max_idx] = pool.MaxClearance(test_poly.value());
  Print("Exact root poly max pool clearance = {:.17g} (pose index {})\n", exact_max_c, exact_max_idx);

  // Print face normals and their vertices
  Print("--- Faces of nopert_214 with normals ---\n");
  for (size_t fi = 0; fi < poly214.faces->v.size(); fi++) {
    const auto &fv = poly214.faces->v[fi];
    vec3 n = yocto::normalize(yocto::cross(
        poly214.vertices[fv[1]] - poly214.vertices[fv[0]],
        poly214.vertices[fv[2]] - poly214.vertices[fv[0]]));
    double d = yocto::dot(n, poly214.vertices[fv[0]]);
    if (d < 0) { n = -n; d = -d; }
    Print("  Face {:2d} (size {}): n = ({:+.6f}, {:+.6f}, {:+.6f}), d = {:.10f}\n",
          fi, fv.size(), n.x, n.y, n.z, d);
  }

  // 3. Extract exact symmetric generators from #214
  SymmetricPolyHalfspaces sym = SymmetricPolyHalfspaces::FromPolyhedron(poly214);
  Print("Extracted {} axial caps and {} generators (order {}).\n",
        sym.axial_caps.size(), sym.generators.size(), sym.SYMMETRY_ORDER);

  for (size_t i = 0; i < sym.axial_caps.size(); i++) {
    Print("  Cap {}: n = ({:.6f}, {:.6f}, {:.6f}), d = {:.10f}\n",
          i, sym.axial_caps[i].normal.x, sym.axial_caps[i].normal.y,
          sym.axial_caps[i].normal.z, sym.axial_caps[i].d);
  }
  for (size_t i = 0; i < sym.generators.size(); i++) {
    Print("  Gen {}: n = ({:.6f}, {:.6f}, {:.6f}), d = {:.10f}\n",
          i, sym.generators[i].normal.x, sym.generators[i].normal.y,
          sym.generators[i].normal.z, sym.generators[i].d);
  }

  // 4. Test reconstruction from symmetry generators
  std::vector<HalfSpace> all_sym_planes = sym.AllHalfspaces();
  Print("Generated {} symmetric half-spaces.\n", all_sym_planes.size());

  // Check distance of original 20 vertices to all 27 halfspaces
  Print("Checking original 20 vertices against 27 halfspaces:\n");
  for (size_t vi = 0; vi < poly214.vertices.size(); vi++) {
    int tight_faces = 0;
    for (const auto &hs : all_sym_planes) {
      if (std::abs(hs.SignedDistance(poly214.vertices[vi])) < 1e-6) {
        tight_faces++;
      }
    }
    Print("  v[{:2d}] touches {} planes\n", vi, tight_faces);
  }

  // Also check which planes touch how many vertices
  Print("Checking 27 halfspaces against 20 vertices:\n");
  for (size_t pi = 0; pi < all_sym_planes.size(); pi++) {
    int touching = 0;
    for (const auto &v : poly214.vertices) {
      if (std::abs(all_sym_planes[pi].SignedDistance(v)) < 1e-6) {
        touching++;
      }
    }
    Print("  Plane {:2d}: touches {} vertices\n", pi, touching);
  }

  auto reconstructed = HalfspacesToPolyhedron(all_sym_planes, "sym_recon_214");
  if (!reconstructed.has_value()) {
    Print(ARED("Failed to reconstruct symmetric polyhedron from generators!\n"));
    return;
  }

  Print("Reconstructed polyhedron from generators: {} vertices, {} faces.\n",
        reconstructed->vertices.size(),
        reconstructed->faces != nullptr ? (int)reconstructed->faces->v.size() : 0);
  auto [recon_c, recon_idx] = pool.MaxClearance(reconstructed.value());
  Print("Symmetric reconstructed check: max pool clearance = {:.17g} (pose index {})\n",
        recon_c, recon_idx);

  // 5. Test LocalTiltAttack on unperturbed baseline
  Print("\nAttacking unperturbed #214 locally near all 12 solution poses...\n");
  auto base_attack = pool.LocalTiltAttack(reconstructed.value(), 40);
  Print("Baseline attack result: survived = {}, max_c = {:+.6e} (pose {})\n",
        base_attack.survived, base_attack.max_achieved_clearance, base_attack.worst_pose_idx);

  // Helper to build a 5-fold cyclically symmetric polyhedron from 4 root vertices,
  // algebraically enforcing exact coplanarity of the quad face.
  auto PolyhedronFromRoot = [](std::array<vec3, 4> root,
                               std::string_view name) -> std::optional<Polyhedron> {
    const vec3 &r0 = root[0];
    const vec3 &r1 = root[1];
    const vec3 &r2 = root[2];

    double theta = -2.0 * std::numbers::pi / 5.0;
    double cos_t = std::cos(theta), sin_t = std::sin(theta);
    vec3 r1_rot(cos_t * r1.x - sin_t * r1.y, sin_t * r1.x + cos_t * r1.y, r1.z);

    double dx_rot = r1_rot.x - r1.x;
    double dy_rot = r1_rot.y - r1.y;

    double C1 = (r2.x - r0.x) * dy_rot - (r2.y - r0.y) * dx_rot;
    if (std::abs(C1) < 1e-9) return std::nullopt;
    double C2 = (r2.z - r0.z) * ((r1.x - r0.x) * dy_rot - (r1.y - r0.y) * dx_rot);
    root[1].z = r0.z + C2 / C1;

    std::vector<vec3> verts;
    verts.reserve(20);
    for (int level = 0; level < 4; level++) {
      for (int k = 0; k < 5; k++) {
        double angle = (2.0 * std::numbers::pi * k) / 5.0;
        double c = std::cos(angle), s = std::sin(angle);
        verts.emplace_back(
            c * root[level].x - s * root[level].y,
            s * root[level].x + c * root[level].y,
            root[level].z);
      }
    }
    return PolyhedronFromVertices(verts, std::string(name));
  };

  auto IsValid20V27FTopology = [](const Polyhedron &poly) -> bool {
    if (poly.vertices.size() != 20) return false;
    if (poly.faces == nullptr || poly.faces->v.size() != 27) return false;
    int c3 = 0, c4 = 0, c5 = 0;
    for (const auto &f : poly.faces->v) {
      if (f.size() == 3) c3++;
      else if (f.size() == 4) c4++;
      else if (f.size() == 5) c5++;
      else return false;
    }
    return (c3 == 20 && c4 == 5 && c5 == 2);
  };

  auto RotateZ = [](vec3 v, double d_rad) -> vec3 {
    double c = std::cos(d_rad), s = std::sin(d_rad);
    return vec3{c * v.x - s * v.y, s * v.x + c * v.y, v.z};
  };

  std::array<vec3, 4> base_root = {
      poly214.vertices[0],
      poly214.vertices[5],
      poly214.vertices[10],
      poly214.vertices[15]
  };

  Print("\n--- Surveying Root Perturbations (Strict 20-vertex, 27-face) ---\n");

  enum class RootParam {
    TOP_CAP_Z,
    TOP_CAP_RADIUS,
    BOTTOM_CAP_Z,
    BOTTOM_CAP_RADIUS,
    RING2_Z,
    RING2_RADIUS,
    RING1_RADIUS,
    RING1_ANGLE,
  };

  struct RootParamInfo {
    RootParam param;
    const char *name;
  };

  const RootParamInfo root_params[] = {
      {RootParam::TOP_CAP_Z, "Top Cap Height (r3.z)"},
      {RootParam::TOP_CAP_RADIUS, "Top Cap Radius (r3.xy)"},
      {RootParam::BOTTOM_CAP_Z, "Bottom Cap Height (r0.z)"},
      {RootParam::BOTTOM_CAP_RADIUS, "Bottom Cap Radius (r0.xy)"},
      {RootParam::RING2_Z, "Ring 2 Height (r2.z)"},
      {RootParam::RING2_RADIUS, "Ring 2 Radius (r2.xy)"},
      {RootParam::RING1_RADIUS, "Ring 1 Radius (r1.xy)"},
      {RootParam::RING1_ANGLE, "Ring 1 Angle (r1 azimuth)"},
  };

  for (const auto &pinfo : root_params) {
    Print("Parameter: {}\n", pinfo.name);
    for (double delta : {-1e-3, -5e-4, -2e-4, -1e-4, -5e-5, +5e-5, +1e-4, +2e-4, +5e-4, +1e-3}) {
      std::array<vec3, 4> test_root = base_root;
      switch (pinfo.param) {
      case RootParam::TOP_CAP_Z:
        test_root[3].z += delta;
        break;
      case RootParam::TOP_CAP_RADIUS:
        test_root[3].x *= (1.0 + delta);
        test_root[3].y *= (1.0 + delta);
        break;
      case RootParam::BOTTOM_CAP_Z:
        test_root[0].z += delta;
        break;
      case RootParam::BOTTOM_CAP_RADIUS:
        test_root[0].x *= (1.0 + delta);
        test_root[0].y *= (1.0 + delta);
        break;
      case RootParam::RING2_Z:
        test_root[2].z += delta;
        break;
      case RootParam::RING2_RADIUS:
        test_root[2].x *= (1.0 + delta);
        test_root[2].y *= (1.0 + delta);
        break;
      case RootParam::RING1_RADIUS:
        test_root[1].x *= (1.0 + delta);
        test_root[1].y *= (1.0 + delta);
        break;
      case RootParam::RING1_ANGLE: {
        double c = std::cos(delta), s = std::sin(delta);
        double nx = c * test_root[1].x - s * test_root[1].y;
        double ny = s * test_root[1].x + c * test_root[1].y;
        test_root[1].x = nx;
        test_root[1].y = ny;
        break;
      }
      }

      auto cand = PolyhedronFromRoot(test_root, "root_survey");
      if (!cand.has_value()) {
        Print("  delta {:+.1e}: failed reconstruction\n", delta);
        continue;
      }
      int nv = (int)cand->vertices.size();
      int nf = cand->faces != nullptr ? (int)cand->faces->v.size() : 0;
      auto [c, pose_idx] = pool.MaxClearance(cand.value());
      Print("  delta {:+.1e}: V={}, F={}, max_pool_c = {:+.6e} (pose {})\n",
            delta, nv, nf, c, pose_idx);
    }
  }

  // 6. Define candidate polyhedra using root perturbations that strictly preserve 20 vertices and 27 faces
  struct RootCandidateSpec {
    std::string id;
    std::string label;
    std::function<std::array<vec3, 4>(const std::array<vec3, 4>&)> transform;
  };

  std::vector<RootCandidateSpec> candidates = {
      // Historical benchmark controls (both verified cracked in DB: #1660 and #1662)
      {"R2", "Benchmark R2: Top Cap Height -1e-3 (Single Param)",
       [](std::array<vec3, 4> r) { r[3].z -= 1e-3; return r; }},
      {"R8", "Benchmark R8: Ring 1 Angle -1e-3 (Single Param)",
       [&](std::array<vec3, 4> r) { r[1] = RotateZ(r[1], -1e-3); return r; }},

      // M9 Anchor and Systematic Variations
      {"M9", "Compound M9: Ring 1 Angle -1e-3, Ring 2 Radius +1e-3, Top Cap -1e-3",
       [&](std::array<vec3, 4> r) {
         r[1] = RotateZ(r[1], -1e-3);
         r[2].x *= (1.0 + 1e-3);
         r[2].y *= (1.0 + 1e-3);
         r[3].z -= 1e-3;
         return r;
       }},
      {"M9b", "Compound M9b: Ring 1 Angle -1.5e-3, Ring 2 Radius +1.5e-3, Top Cap -1.5e-3",
       [&](std::array<vec3, 4> r) {
         r[1] = RotateZ(r[1], -1.5e-3);
         r[2].x *= (1.0 + 1.5e-3);
         r[2].y *= (1.0 + 1.5e-3);
         r[3].z -= 1.5e-3;
         return r;
       }},
      {"M9c", "Compound M9c: Ring 1 Angle -1e-3, Ring 2 Radius +1e-3, Top Cap -1.5e-3",
       [&](std::array<vec3, 4> r) {
         r[1] = RotateZ(r[1], -1e-3);
         r[2].x *= (1.0 + 1e-3);
         r[2].y *= (1.0 + 1e-3);
         r[3].z -= 1.5e-3;
         return r;
       }},
      {"M9d", "Compound M9d: Ring 1 Angle -1e-3, Ring 2 Radius +1.5e-3, Top Cap -1e-3",
       [&](std::array<vec3, 4> r) {
         r[1] = RotateZ(r[1], -1e-3);
         r[2].x *= (1.0 + 1.5e-3);
         r[2].y *= (1.0 + 1.5e-3);
         r[3].z -= 1e-3;
         return r;
       }},
      {"M9e", "Compound M9e: M9 + Bottom Cap Height +2e-4",
       [&](std::array<vec3, 4> r) {
         r[1] = RotateZ(r[1], -1e-3);
         r[2].x *= (1.0 + 1e-3);
         r[2].y *= (1.0 + 1e-3);
         r[3].z -= 1e-3;
         r[0].z += 2e-4;
         return r;
       }},
      {"M9f", "Compound M9f: M9 + Bottom Cap Radius -1e-3",
       [&](std::array<vec3, 4> r) {
         r[1] = RotateZ(r[1], -1e-3);
         r[2].x *= (1.0 + 1e-3);
         r[2].y *= (1.0 + 1e-3);
         r[3].z -= 1e-3;
         r[0].x *= (1.0 - 1e-3);
         r[0].y *= (1.0 - 1e-3);
         return r;
       }},
      {"M9g", "Compound M9g: M9 + Ring 2 Angle +5e-4",
       [&](std::array<vec3, 4> r) {
         r[1] = RotateZ(r[1], -1e-3);
         r[2] = RotateZ(r[2], +5e-4);
         r[2].x *= (1.0 + 1e-3);
         r[2].y *= (1.0 + 1e-3);
         r[3].z -= 1e-3;
         return r;
       }},
      {"M9h", "Compound M9h: M9 + Ring 1 Radius -5e-4",
       [&](std::array<vec3, 4> r) {
         r[1] = RotateZ(r[1], -1e-3);
         r[1].x *= (1.0 - 5e-4);
         r[1].y *= (1.0 - 5e-4);
         r[2].x *= (1.0 + 1e-3);
         r[2].y *= (1.0 + 1e-3);
         r[3].z -= 1e-3;
         return r;
       }},
      {"M9i", "Compound M9i: M9 + Ring 2 Height -5e-4",
       [&](std::array<vec3, 4> r) {
         r[1] = RotateZ(r[1], -1e-3);
         r[2].x *= (1.0 + 1e-3);
         r[2].y *= (1.0 + 1e-3);
         r[2].z -= 5e-4;
         r[3].z -= 1e-3;
         return r;
       }},
      {"M9j", "Compound M9j: M9 + Ring 2 Height +5e-4",
       [&](std::array<vec3, 4> r) {
         r[1] = RotateZ(r[1], -1e-3);
         r[2].x *= (1.0 + 1e-3);
         r[2].y *= (1.0 + 1e-3);
         r[2].z += 5e-4;
         r[3].z -= 1e-3;
         return r;
       }},
      {"M9k", "Compound M9k: Ring 1 Angle -1e-3, Ring 2 Radius +2e-3, Top Cap -1e-3",
       [&](std::array<vec3, 4> r) {
         r[1] = RotateZ(r[1], -1e-3);
         r[2].x *= (1.0 + 2e-3);
         r[2].y *= (1.0 + 2e-3);
         r[3].z -= 1e-3;
         return r;
       }},
      {"M9L", "Compound M9L: Ring 1 Angle -1e-3, Ring 2 Radius +2e-3, Top Cap -2e-3",
       [&](std::array<vec3, 4> r) {
         r[1] = RotateZ(r[1], -1e-3);
         r[2].x *= (1.0 + 2e-3);
         r[2].y *= (1.0 + 2e-3);
         r[3].z -= 2e-3;
         return r;
       }},
  };

  Print(AYELLOW("\n=======================================================\n"));
  Print(AYELLOW("  EVALUATING ROOT CANDIDATE NOPERTS (20V, 27F)        \n"));
  Print(AYELLOW("  Duration: {:.0f}s ({:.2f}m) per candidate | Threads: {:d}     \n"),
        options.seconds_per_candidate, options.seconds_per_candidate / 60.0,
        options.num_threads);
  Print(AYELLOW("=======================================================\n\n"));

  for (const auto &spec : candidates) {
    if (options.candidate != "all" && options.candidate != spec.id) {
      continue;
    }

    Print(AWHITE("\nTesting {}: {}...\n"), spec.id, spec.label);
    std::array<vec3, 4> cand_root = spec.transform(base_root);
    auto cand_poly = PolyhedronFromRoot(cand_root, spec.label);
    if (!cand_poly) {
      Print(ARED("  Failed: reconstruction failed\n"));
      continue;
    }
    if (!IsValid20V27FTopology(cand_poly.value())) {
      Print(ARED("  Failed: topology changed! vertices={}, faces={}\n"),
            cand_poly->vertices.size(),
            cand_poly->faces != nullptr ? (int)cand_poly->faces->v.size() : 0);
      continue;
    }
    Print("  Polyhedron reconstructed: {} vertices, {} faces. Volume = {:.10f}\n",
          cand_poly->vertices.size(),
          cand_poly->faces != nullptr ? (int)cand_poly->faces->v.size() : 0,
          Volume(cand_poly.value()));

    // Check 1: Pool clearances
    auto [pool_max_c, pool_idx] = pool.MaxClearance(cand_poly.value());
    if (pool_idx >= 0) {
      Print("  Rejection pool check: max pool c = {:+.6e} (pose {})\n", pool_max_c, pool_idx);
    } else {
      Print(AGREEN("  Rejection pool check: all 12 solution poses defeated! (c <= 0)\n"));
    }

    // Check 2: Deep Local Tilt Attack (80 iterations per known pose)
    auto local_attack = pool.LocalTiltAttack(cand_poly.value(), 80);
    Print("  Local tilt attack (12 poses, 80 iters): survived = {}, max_c = {:+.6e}\n",
          local_attack.survived, local_attack.max_achieved_clearance);

    if (!local_attack.survived) {
      Print(ARED("  Cracked by local tilt attack!\n"));
      continue;
    }

    if (options.insert_db) {
      int nopert_id = db.AddNopert(cand_poly.value(), SolutionDB::NOPERT_METHOD_REPAIR214_QUAD);
      Print(AGREEN("  >>> INSERTED into database as nopert_{} (id={}, method=NOPERT_METHOD_REPAIR214_QUAD) <<<\n"),
            nopert_id, nopert_id);
    }

    if (options.pool_only) {
      continue;
    }

    // Check 3: Full multi-threaded CPU TiltGrad solver attack
    Print("  Running {}s multi-threaded TiltGrad solver attack ({} threads)...\n",
          options.seconds_per_candidate, options.num_threads);

    auto tg_res = RunParallelTimedTiltGradAttack(
        cand_poly.value(), options.seconds_per_candidate, options.num_threads,
        spec.id, status);

    if (status != nullptr) {
      status->Clear();
    }

    if (tg_res.cracked) {
      Print(ARED("  CRACKED by TiltGrad solver! Clearance = {:+.6e} after {:.1f}s (trial {})\n"),
            tg_res.best_clearance, tg_res.elapsed_seconds, tg_res.total_trials);
      Print("  Outer frame:\n    x: ({}, {}, {})\n    y: ({}, {}, {})\n    z: ({}, {}, {})\n    o: ({}, {}, {})\n",
            tg_res.best_outer.x.x, tg_res.best_outer.x.y, tg_res.best_outer.x.z,
            tg_res.best_outer.y.x, tg_res.best_outer.y.y, tg_res.best_outer.y.z,
            tg_res.best_outer.z.x, tg_res.best_outer.z.y, tg_res.best_outer.z.z,
            tg_res.best_outer.o.x, tg_res.best_outer.o.y, tg_res.best_outer.o.z);
      Print("  Inner frame:\n    x: ({}, {}, {})\n    y: ({}, {}, {})\n    z: ({}, {}, {})\n    o: ({}, {}, {})\n",
            tg_res.best_inner.x.x, tg_res.best_inner.x.y, tg_res.best_inner.x.z,
            tg_res.best_inner.y.x, tg_res.best_inner.y.y, tg_res.best_inner.y.z,
            tg_res.best_inner.z.x, tg_res.best_inner.z.y, tg_res.best_inner.z.z,
            tg_res.best_inner.o.x, tg_res.best_inner.o.y, tg_res.best_inner.o.z);
    } else {
      double rate = tg_res.total_trials / std::max(0.001, tg_res.elapsed_seconds);
      Print(AGREEN("  [ROBUST NOPERT CANDIDATE] Survives continuous solver attack for {:.1f}s ({:.1f}m)!\n"),
            tg_res.elapsed_seconds, tg_res.elapsed_seconds / 60.0);
      Print("  Total trials evaluated across {} threads: {} ({:.0f} trials/sec)\n",
            options.num_threads, tg_res.total_trials, rate);
      Print("  Best clearance over all trials: {:+.6e}\n", tg_res.best_clearance);
      Print("  Defense margin against known solutions: {:+.6e}\n",
            -local_attack.max_achieved_clearance);

      Print("\n  Candidate 4 Root Vertices:\n");
      for (size_t ri = 0; ri < 4; ri++) {
        Print("    r[{:d}] = ({:+.10f}, {:+.10f}, {:+.10f})\n",
              ri, cand_root[ri].x, cand_root[ri].y, cand_root[ri].z);
      }

      Print("\n  Candidate Vertices ({} vertices):\n", cand_poly->vertices.size());
      for (size_t v_idx = 0; v_idx < cand_poly->vertices.size(); v_idx++) {
        const auto &v = cand_poly->vertices[v_idx];
        Print("    v[{:2d}] = ({:+.10f}, {:+.10f}, {:+.10f})\n",
              v_idx, v.x, v.y, v.z);
      }
      Print("\n");
    }
  }
}

int main(int argc, char **argv) {
  ANSI::Init();

  Options options;
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      Print("Usage: ./repair214.exe [options]\n"
            "Options:\n"
            "  --candidate <R1..R9|all>       Candidate to evaluate (default: all)\n"
            "  --seconds <N>                  Seconds per candidate (default: 1800 = 30m)\n"
            "  --threads <N>                  Worker threads (default: 8)\n"
            "  --quick                        Quick smoke test (5 seconds per candidate)\n"
            "  --pool-only                    Only test known solution pool and local tilt attack\n");
      return 0;
    } else if (arg == "--candidate" && i + 1 < argc) {
      options.candidate = argv[++i];
    } else if (arg == "--seconds" && i + 1 < argc) {
      options.seconds_per_candidate = std::stod(argv[++i]);
    } else if (arg == "--threads" && i + 1 < argc) {
      options.num_threads = std::stoi(argv[++i]);
    } else if (arg == "--quick") {
      options.quick = true;
      options.seconds_per_candidate = 5.0;
    } else if (arg == "--pool-only") {
      options.pool_only = true;
    } else if (arg == "--insert-db") {
      options.insert_db = true;
    } else {
      Print("Unknown argument: {}\n", arg);
      return 1;
    }
  }

  StatusBar status(1);
  Repair214(options, &status);

  return 0;
}
