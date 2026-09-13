// Proof search engine for Nopert #229 formal verification in Lean 4.
// Explores the 5D pose space (Cayley chart R³ × projective view S²) via
// branch-and-bound, accelerated by OpenCL on GPU (or multi-threaded CPU).
//
// Generates certificate trees (.rows.log and .pack) that can be verified
// directly by constructNopert229 in Lean.
//
// Evaluates all candidate triples directly on the GPU/CPU without per-box
// candidate ranking or sorting overhead. Unique triangle pools are deduplicated
// per batch, and threads loop over pool triples with early exit on certificate.

#include <CL/cl.h>
#include <CL/cl_platform.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <iterator>
#include <list>
#include <memory>
#include <mutex>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ansi.h"
#include "atomic-util.h"
#include "base/logging.h"
#include "base/print.h"
#include "geom/hull-2d.h"
#include "geom/polyhedra.h"
#include "opencl/clutil.h"
#include "periodically.h"
#include "ruperts-util.h"
#include "solutions.h"
#include "status-bar.h"
#include "threadutil.h"
#include "timer.h"
#include "util.h"
#include "yocto-math.h"

DECLARE_COUNTERS(evaluated_count, certified_count, pruned_count, split_count,
                 ctr_built_triangles, ctr_loops, ctr_cpu, ctr_esc_certified);
DECLARE_COUNTERS(difficult_count);

using vec2 = yocto::vec<double, 2>;
using vec3 = yocto::vec<double, 3>;

static CL *cl = nullptr;
static std::atomic<bool> sigint_received{false};

static void SigHandler(int) {
  sigint_received.store(true);
}

// 20 vertices of Nopert #229, derived algebraically from repair214.cc
// and scaled strictly inside the unit sphere for Lean's GoodPoly invariant.
static constexpr int NUM_VERTICES = 20;
static constexpr double VERTICES[NUM_VERTICES][3] = {
  {  0.0428407320766475,  0.5680663556187131,  0.5648167326177671 }, // 0
  { -0.1710940528198280,  0.9384169756351713,  0.3001672949030625 }, // 1
  { -0.2791996671138589,  0.8916783831939151, -0.0420605170858861 }, // 2
  {  0.0581211699562287,  0.6025790913331870, -0.7643399516054460 }, // 3

  { -0.5270246949360691,  0.2162861152231751,  0.5648167326177671 }, // 4
  { -0.9453585496376318,  0.1272666794475643,  0.3001672949030625 }, // 5
  { -0.9343139787381105,  0.0100091111676232, -0.0420605170858861 }, // 6
  { -0.5551263421462106,  0.2414836970985378, -0.7643399516054460 }, // 7

  { -0.3685599064576828, -0.4343941851161148,  0.5648167326177671 }, // 8
  { -0.4131696624115331, -0.8597618421012388,  0.3001672949030625 }, // 9
  { -0.2982381279104400, -0.8854924122951479, -0.0420605170858861 }, // 10
  { -0.4012081174529903, -0.4533339587973062, -0.7643399516054460 }, // 11

  {  0.2992421458547392, -0.4847564861402479,  0.5648167326177671 }, // 12
  {  0.6900056551469845, -0.6586287200963504,  0.3001672949030625 }, // 13
  {  0.7499926789483198, -0.5572735187461599, -0.0420605170858861 }, // 14
  {  0.3071660889979028, -0.5216594918898176, -0.7643399516054460 }, // 15

  {  0.5535017234623651,  0.1347982004144742,  0.5648167326177671 }, // 16
  {  0.8396166097220085,  0.4527069071148534,  0.3001672949030625 }, // 17
  {  0.7617590948140895,  0.5410784366797694, -0.0420605170858861 }, // 18
  {  0.5910472006450693,  0.1309306622553988, -0.7643399516054460 }  // 19
};

// Lean 4 displacement error parameters:
// In AtlasProjectiveGlobalCertificate.lean:
//   Box.displacementError = 300 * box.dBound * tightVertexErrorQ
//
// How this constant is chosen and justified:
// 1. tightVertexErrorQ = 6 / 10^16:
//    Proved algebraically in TightApproximation.lean (theorem vertex_close_tight):
//      ||exactVertex i - toR3 (rationalVertex i)|| <= (tightVertexErrorQ : R)
//    This bounds the Euclidean distance between Nopert #229's exact algebraic
//    vertices (roots of degree-16 polynomials) and the 16-decimal-digit rational
//    approximations used for verified arithmetic.
// 2. Factor of 300:
//    Proved in AtlasProjectiveGlobalCertificate.lean (theorem clearedDisplacement_error):
//    The cleared displacement is a sum over 3 contact planes in the Farkas cone.
//    For each contact i in Fin 3, theorem weightedContact_error bounds the error by
//    100 * dBound * tightVertexErrorQ (accounting for vertex displacement error,
//    approximate edge cross products, view vector dot products, and cone weights).
//    Summing over all 3 contacts gives 3 * 100 = 300 * dBound * tightVertexErrorQ.
//
// In double precision, 300 * 6e-16 = 1.8e-13.
// We export TIGHT_VERTEX_ERROR to OpenCL via CLPreamble() so CPU and GPU
// evaluations use the exact same constant from this single source of truth.
static constexpr double TIGHT_VERTEX_ERROR = 6e-16;

// Binary formats matching OpenCL kernel layouts
struct GpuBox {
  double cx, cy, cz;
  double rx, ry, rz;
  double tri[3][3];
  int chart;
  int num_triples;
  int triple_offset;
  int contact_offset;
  int num_contacts;
  int _pad;
};
static_assert(sizeof(GpuBox) == 144, "GpuBox struct size mismatch");

struct GpuContact {
  int vertex;
  double edge[3];
  double defect;
};

struct GpuTriple {
  // Local contact indices in this pool (0 to num_contacts - 1)
  uint8_t c0, c1, c2;
  uint8_t _pad[5];
  double weighted_defect_upper;
};
static_assert(sizeof(GpuTriple) == 16, "GpuTriple struct size mismatch");

struct GpuResult {
  int certified;
  int winning_triple;
  int inner[3];
  double margin;
};
static_assert(sizeof(GpuResult) == 32, "GpuResult struct size mismatch");

static constexpr int MAX_GPU_CONTACTS = 256;

// Projective view triangle
struct ProjectiveTriangle {
  std::array<vec3, 3> corners;

  vec3 Centroid() const {
    return (corners[0] + corners[1] + corners[2]) / 3.0;
  }

  double AngularDiameter() const {
    vec3 u0 = yocto::normalize(corners[0]);
    vec3 u1 = yocto::normalize(corners[1]);
    vec3 u2 = yocto::normalize(corners[2]);
    double d01 = yocto::length(u0 - u1);
    double d12 = yocto::length(u1 - u2);
    double d20 = yocto::length(u2 - u0);
    return std::max({d01, d12, d20});
  }

  std::array<ProjectiveTriangle, 4> Subdivide() const {
    vec3 m01 = (corners[0] + corners[1]) * 0.5;
    vec3 m12 = (corners[1] + corners[2]) * 0.5;
    vec3 m20 = (corners[2] + corners[0]) * 0.5;
    return {{
      {corners[0], m01, m20},
      {m01, corners[1], m12},
      {m20, m12, corners[2]},
      {m01, m12, m20}
    }};
  }

  bool ContainsRay(const vec3 &v) const {
    double det = yocto::dot(corners[0], yocto::cross(corners[1], corners[2]));
    if (std::abs(det) < 1e-15) return false;
    double s = (det > 0.0) ? 1.0 : -1.0;
    double d0 = s * yocto::dot(v, yocto::cross(corners[1], corners[2]));
    double d1 = s * yocto::dot(v, yocto::cross(corners[2], corners[0]));
    double d2 = s * yocto::dot(v, yocto::cross(corners[0], corners[1]));
    return d0 >= -1e-12 && d1 >= -1e-12 && d2 >= -1e-12;
  }
};

// 3D Cayley box
struct CayleyBox {
  vec3 center;
  vec3 radii;

  bool Contains(const vec3 &w) const {
    return std::abs(w.x - center.x) <= radii.x + 1e-12 &&
           std::abs(w.y - center.y) <= radii.y + 1e-12 &&
           std::abs(w.z - center.z) <= radii.z + 1e-12;
  }

  int WidestAxis() const {
    int axis = 0;
    if (radii.y > radii[axis]) axis = 1;
    if (radii.z > radii[axis]) axis = 2;
    return axis;
  }

  std::pair<CayleyBox, CayleyBox> Split(int axis) const {
    CayleyBox left = *this;
    CayleyBox right = *this;
    left.radii[axis] *= 0.5;
    right.radii[axis] *= 0.5;
    left.center[axis] -= left.radii[axis];
    right.center[axis] += right.radii[axis];
    return {left, right};
  }

  bool ContainsOrigin() const {
    return std::abs(center.x) <= radii.x &&
           std::abs(center.y) <= radii.y &&
           std::abs(center.z) <= radii.z;
  }
};

static inline bool OutsideBall(const CayleyBox &b) {
  double d2 = 0.0;
  for (int c = 0; c < 3; c++) {
    double v = std::abs(b.center[c]) - b.radii[c];
    if (v > 0.0) d2 += v * v;
  }
  return d2 > 1.0;
}

struct FundamentalPruneResult {
  bool prune = false;
  int direction = 0; // -1 or 1
};

static inline FundamentalPruneResult CheckFundamentalPrune(
    int chart, const CayleyBox &b) {
  // 1. Restricted root interval bounds:
  // Chart 0: z in [-1/3, 1/3]
  // Chart 1: y in [-1/3, 1/3]
  // Chart 2: x in [-1/3, 1/3]
  if (chart == 0) {
    if (b.center.z - b.radii.z > 1.0 / 3.0) return {true, -1};
    if (b.center.z + b.radii.z < -1.0 / 3.0) return {true, 1};
  } else if (chart == 1) {
    if (b.center.y - b.radii.y > 1.0 / 3.0) return {true, 1};
    if (b.center.y + b.radii.y < -1.0 / 3.0) return {true, -1};
  } else if (chart == 2) {
    if (b.center.x - b.radii.x > 1.0 / 3.0) return {true, 1};
    if (b.center.x + b.radii.x < -1.0 / 3.0) return {true, -1};
  }

  // 2. Advantage quadratics against fivefold rotations.
  // (Direction::negative = -1, positive = +1):
  const double K_a = -690983.0 / 1000000.0;
  const double K_b_base = 951057.0 / 1000000.0;
  const double approx_error = 2.0 / 125000.0; // 1.6e-5

  for (int dir : {-1, 1}) {
    double K_b = dir * K_b_base;
    double c0 = 0.0, cx = 0.0, cy = 0.0, cz = 0.0;
    double cxx = 0.0, cyy = 0.0, czz = 0.0, cxy = 0.0;

    if (chart == 0) {
      c0 = 2.0 * K_a;
      cz = -4.0 * K_b;
      czz = -2.0 * K_a;
    } else if (chart == 1) {
      cxx = 2.0 * K_a;
      cyy = -2.0 * K_a;
      cxy = 4.0 * K_b;
    } else if (chart == 2) {
      cxx = -2.0 * K_a;
      cyy = 2.0 * K_a;
      cxy = -4.0 * K_b;
    }

    double x = b.center.x, y = b.center.y, z = b.center.z;
    double rx = b.radii.x, ry = b.radii.y, rz = b.radii.z;

    double val0 = c0 + cx*x + cy*y + cz*z +
                  cxx*x*x + cyy*y*y + czz*z*z + cxy*x*y;

    double gx = cx + 2.0*cxx*x + cxy*y;
    double gy = cy + cxy*x + 2.0*cyy*y;
    double gz = cz + 2.0*czz*z;

    double center =
        val0 + 0.5 * (cxx * rx * rx + cyy * ry * ry + czz * rz * rz);
    double radius =
        (std::abs(gx) * rx + std::abs(gy) * ry + std::abs(gz) * rz) +
        std::abs(cxy) * rx * ry +
        0.5 * (std::abs(cxx) * rx * rx + std::abs(cyy) * ry * ry +
               std::abs(czz) * rz * rz);

    double lower = center - radius;
    if (lower > approx_error) {
      return {true, dir};
    }
  }

  return {false, 0};
}

static inline bool InsideIdentityTube(int chart, const CayleyBox &b,
                                      double tube_r = 1e-4) {
  if (chart != 0) return false;
  double mx = std::abs(b.center.x) + b.radii.x;
  double my = std::abs(b.center.y) + b.radii.y;
  double mz = std::abs(b.center.z) + b.radii.z;
  return 2.0 * std::sqrt(mx*mx + my*my + mz*mz) <= tube_r;
}

// Full 5D pose node in branch-and-bound search
struct SearchNode {
  int64_t id = 0;
  int64_t parent_id = -1;
  CayleyBox box;
  ProjectiveTriangle tri;
  uint8_t depth = 0;
  uint8_t view_depth = 0;
  uint8_t box_depth = 0;
  uint8_t chart = 0;
  uint8_t box_splits_since_view = 0;
};
static_assert(sizeof(SearchNode) == 144);

struct TrianglePool {
  uint64_t id = 0;
  std::vector<GpuContact> contacts;
  std::vector<GpuTriple> gpu_triples;
};

static uint64_t HashTriangle(const ProjectiveTriangle &tri) {
  uint64_t h = 0xcbf29ce484222325ULL;
  for (int c = 0; c < 3; c++) {
    for (int a = 0; a < 3; a++) {
      uint64_t bits;
      std::memcpy(&bits, &tri.corners[c][a], sizeof(bits));
      h ^= bits;
      h *= 0x100000001b3ULL;
    }
  }
  return h;
}

static double ComputeWeightedDefectUpper(const ProjectiveTriangle &tri,
                                         const std::vector<GpuContact> &contacts,
                                         int c0, int c1, int c2,
                                         const vec3 w_coeff[3]) {
  double total = 0.0;
  const double error = 1e-9;
  const int c_indices[3] = {c0, c1, c2};
  for (int i = 0; i < 3; i++) {
    int sel = contacts[c_indices[i]].vertex;
    vec3 edge = {contacts[c_indices[i]].edge[0],
                 contacts[c_indices[i]].edge[1],
                 contacts[c_indices[i]].edge[2]};
    double w_vals[3] = {
      yocto::dot(tri.corners[0], w_coeff[i]) + error,
      yocto::dot(tri.corners[1], w_coeff[i]) + error,
      yocto::dot(tri.corners[2], w_coeff[i]) + error
    };
    double upper = 0.0;
    for (int k = 0; k < NUM_VERTICES; k++) {
      if (k == sel) continue;
      vec3 delta = {
        VERTICES[k][0] - VERTICES[sel][0],
        VERTICES[k][1] - VERTICES[sel][1],
        VERTICES[k][2] - VERTICES[sel][2]
      };
      vec3 s_coeff = yocto::cross(edge, delta);
      double s_vals[3] = {
        yocto::dot(tri.corners[0], s_coeff) + error,
        yocto::dot(tri.corners[1], s_coeff) + error,
        yocto::dot(tri.corners[2], s_coeff) + error
      };
      for (int a = 0; a < 3; a++) {
        for (int b = 0; b < 3; b++) {
          double ctrl = 0.5 * (w_vals[a] * s_vals[b] + w_vals[b] * s_vals[a]);
          if (ctrl > upper) upper = ctrl;
        }
      }
    }
    total += upper;
  }
  return total;
}

static std::shared_ptr<const TrianglePool> BuildTrianglePool(
    const ProjectiveTriangle &tri, int cone_samples = 6, uint64_t id = 0) {
  ctr_built_triangles++;
  auto pool = std::make_shared<TrianglePool>();
  pool->id = id;
  vec3 view = tri.Centroid();
  double len = yocto::length(view);
  if (len < 1e-12) return pool;
  vec3 uview = view / len;

  // Project vertices to 2D along view direction.
  int min_axis = 0;
  for (int a = 1; a < 3; a++) {
    if (std::abs(uview[a]) < std::abs(uview[min_axis])) min_axis = a;
  }
  vec3 axis = {0, 0, 0};
  axis[min_axis] = 1.0;
  vec3 right = yocto::normalize(yocto::cross(uview, axis));
  vec3 up = yocto::cross(uview, right);

  std::vector<vec2> projected(NUM_VERTICES);
  for (int i = 0; i < NUM_VERTICES; i++) {
    vec3 v = {VERTICES[i][0], VERTICES[i][1], VERTICES[i][2]};
    projected[i] = {yocto::dot(v, right), yocto::dot(v, up)};
  }

  std::vector<int> hull = Hull2D::GrahamScan(projected);
  int H = hull.size();
  if (H < 3) return pool;

  // Build silhouette edge contacts interpolating adjacent edges.
  std::vector<GpuContact> valid_contacts;
  for (int i = 0; i < H; i++) {
    int curr = hull[i];
    int next = hull[(i + 1) % H];
    int prev = hull[(i - 1 + H) % H];

    vec3 v_curr = {VERTICES[curr][0], VERTICES[curr][1], VERTICES[curr][2]};
    vec3 v_next = {VERTICES[next][0], VERTICES[next][1], VERTICES[next][2]};
    vec3 v_prev = {VERTICES[prev][0], VERTICES[prev][1], VERTICES[prev][2]};

    vec3 first = v_prev - v_curr;
    vec3 second = v_curr - v_next;

    for (int s = 0; s <= cone_samples + 1; s++) {
      double lam = (double)s / (cone_samples + 1.0);
      vec3 e = lam * first + (1.0 - lam) * second;

      double max_upper = 0.0;
      double min_upper = 1e30;
      for (int k = 0; k < NUM_VERTICES; k++) {
        if (k == curr) continue;
        vec3 delta = {VERTICES[k][0] - VERTICES[curr][0],
                      VERTICES[k][1] - VERTICES[curr][1],
                      VERTICES[k][2] - VERTICES[curr][2]};
        vec3 coeff = yocto::cross(e, delta);
        if (yocto::length(coeff) < 1e-12) continue;
        double upper = std::max({yocto::dot(tri.corners[0], coeff),
                                 yocto::dot(tri.corners[1], coeff),
                                 yocto::dot(tri.corners[2], coeff)});
        if (upper > max_upper) max_upper = upper;
        if (upper < min_upper) min_upper = upper;
      }
      if (min_upper >= 0.0) {
        // Not a valid silhouette support direction.
        continue;
      }

      // Check for duplicate contact direction already in valid_contacts
      vec3 e_norm = yocto::normalize(e);
      bool duplicate = false;
      for (const auto &ex : valid_contacts) {
        vec3 ex_norm = yocto::normalize(
            vec3{ex.edge[0], ex.edge[1], ex.edge[2]});
        if (yocto::dot(e_norm, ex_norm) > 1.0 - 1e-9) {
          duplicate = true;
          break;
        }
      }
      if (duplicate) continue;

      GpuContact c;
      c.vertex = curr;
      c.edge[0] = e.x; c.edge[1] = e.y; c.edge[2] = e.z;
      c.defect = std::max(0.0, max_upper);
      valid_contacts.push_back(c);
    }
  }

  pool->contacts = std::move(valid_contacts);
  int C = pool->contacts.size();
  CHECK_LE(C, MAX_GPU_CONTACTS)
      << "Triangle pool has " << C << " contacts, exceeding MAX_GPU_CONTACTS ("
      << MAX_GPU_CONTACTS << ")";

  pool->gpu_triples.reserve(C * (C - 1) * (C - 2) / 6);

  for (int i = 0; i < C; i++) {
    for (int j = i + 1; j < C; j++) {
      for (int k = j + 1; k < C; k++) {
        vec3 e0 = {
          pool->contacts[i].edge[0],
          pool->contacts[i].edge[1],
          pool->contacts[i].edge[2],
        };
        vec3 e1 = {
          pool->contacts[j].edge[0],
          pool->contacts[j].edge[1],
          pool->contacts[j].edge[2],
        };
        vec3 e2 = {
          pool->contacts[k].edge[0],
          pool->contacts[k].edge[1],
          pool->contacts[k].edge[2],
        };

        vec3 coeff0 = yocto::cross(e1, e2);
        vec3 coeff1 = yocto::cross(e2, e0);
        vec3 coeff2 = yocto::cross(e0, e1);

        double p0 = yocto::dot(view, coeff0);
        double p1 = yocto::dot(view, coeff1);
        double p2 = yocto::dot(view, coeff2);

        int ci = i, cj = j, ck = k;
        if (p0 < 0.0 && p1 < 0.0 && p2 < 0.0) {
          std::swap(cj, ck);
          std::swap(e1, e2);
          coeff0 = yocto::cross(e1, e2);
          coeff1 = yocto::cross(e2, e0);
          coeff2 = yocto::cross(e0, e1);
          p0 = yocto::dot(view, coeff0);
          p1 = yocto::dot(view, coeff1);
          p2 = yocto::dot(view, coeff2);
        }

        if (p0 <= 1e-9 || p1 <= 1e-9 || p2 <= 1e-9) continue;

        // Check corner non-negativity across triangle
        double w0_min = std::min({yocto::dot(tri.corners[0], coeff0),
                                  yocto::dot(tri.corners[1], coeff0),
                                  yocto::dot(tri.corners[2], coeff0)});
        double w1_min = std::min({yocto::dot(tri.corners[0], coeff1),
                                  yocto::dot(tri.corners[1], coeff1),
                                  yocto::dot(tri.corners[2], coeff1)});
        double w2_min = std::min({yocto::dot(tri.corners[0], coeff2),
                                  yocto::dot(tri.corners[1], coeff2),
                                  yocto::dot(tri.corners[2], coeff2)});
        // Ensure the view cone triangle corners are strictly interior to the
        // contact cone. We test against a small positive epsilon (1e-11) rather
        // than 0.0 because Lean subtracts supportError = 6/10^15 (accounting
        // for algebraic vs rational vertex differences) when computing
        // weightLower = min_c(w) - supportError >= 0. Testing against <= 1e-11
        // safely eliminates exact-boundary triples (which evaluate to 0.0 in
        // exact arithmetic or +-1e-15 float noise) and leaves ~1600x headroom
        // above supportError, guaranteeing weightLower > 0 in Lean.
        // Note: this epsilon applies only to the angular view weights, not the
        // physical clearance margins between the polyhedra.
        if (w0_min <= 1e-11 || w1_min <= 1e-11 || w2_min <= 1e-11) continue;

        GpuTriple gt;
        gt.c0 = (uint8_t)ci;
        gt.c1 = (uint8_t)cj;
        gt.c2 = (uint8_t)ck;
        std::memset(gt._pad, 0, sizeof(gt._pad));

        vec3 w_coeffs[3] = {coeff0, coeff1, coeff2};
        gt.weighted_defect_upper =
          ComputeWeightedDefectUpper(tri, pool->contacts, ci, cj, ck, w_coeffs);
        pool->gpu_triples.push_back(gt);
      }
    }
  }

  pool->gpu_triples.shrink_to_fit();
  return pool;
}

static size_t g_max_triangle_cache_size = 20480;
static std::mutex g_triangle_cache_mutex;
static std::list<std::shared_ptr<const TrianglePool>> g_triangle_lru_list;
static std::unordered_map<uint64_t, std::list<std::shared_ptr<const TrianglePool>>::iterator>
g_triangle_cache;

static std::shared_ptr<const TrianglePool>
GetTrianglePool(const ProjectiveTriangle &tri, int cone_samples = 6) {
  const uint64_t h =
      HashTriangle(tri) ^ (uint64_t(cone_samples) * 0x9e3779b97f4a7c15ULL);
  {
    MutexLock ml(&g_triangle_cache_mutex);
    auto it = g_triangle_cache.find(h);
    if (it != g_triangle_cache.end()) {
      g_triangle_lru_list.splice(g_triangle_lru_list.begin(),
                                 g_triangle_lru_list, it->second);
      return *(it->second);
    }
  }
  auto pool = BuildTrianglePool(tri, cone_samples, h);
  {
    MutexLock ml(&g_triangle_cache_mutex);
    auto it = g_triangle_cache.find(h);
    if (it != g_triangle_cache.end()) {
      g_triangle_lru_list.splice(g_triangle_lru_list.begin(),
                                 g_triangle_lru_list, it->second);
      return *(it->second);
    }
    if (g_triangle_cache.size() >= g_max_triangle_cache_size) {
      auto oldest = std::prev(g_triangle_lru_list.end());
      g_triangle_cache.erase((*oldest)->id);
      g_triangle_lru_list.pop_back();
    }
    g_triangle_lru_list.push_front(pool);
    g_triangle_cache[h] = g_triangle_lru_list.begin();
    return pool;
  }
}

inline double Bernstein27Min(const double C[10],
                             double lx, double ly, double lz,
                             double wx, double wy, double wz) {
  double a0 = (C[0] + C[1] * lx + C[2] * ly + C[3] * lz + C[4] * lx * lx +
               C[5] * lx * ly + C[6] * lx * lz + C[7] * ly * ly +
               C[8] * ly * lz + C[9] * lz * lz);
  double ax = wx * (C[1] + 2.0 * C[4] * lx + C[5] * ly + C[6] * lz);
  double ay = wy * (C[2] + C[5] * lx + 2.0 * C[7] * ly + C[8] * lz);
  double az = wz * (C[3] + C[6] * lx + C[8] * ly + 2.0 * C[9] * lz);
  double axx = C[4] * wx * wx, ayy = C[7] * wy * wy, azz = C[9] * wz * wz;
  double axy = C[5] * wx * wy, axz = C[6] * wx * wz, ayz = C[8] * wy * wz;

  double min_b = 1e30;
  for (int bi = 0; bi <= 2; bi++) {
    double ti = 0.5 * bi * ax + (bi == 2 ? axx : 0.0);
    for (int bj = 0; bj <= 2; bj++) {
      double tj =
          ti + 0.5 * bj * ay + (bj == 2 ? ayy : 0.0) + 0.25 * bi * bj * axy;
      for (int bk = 0; bk <= 2; bk++) {
        double val = a0 + tj + 0.5 * bk * az + (bk == 2 ? azz : 0.0) +
                     0.25 * bk * (bi * axz + bj * ayz);
        if (val < min_b)
          min_b = val;
      }
    }
  }
  return min_b;
}

// In-register accumulation of the 10 quadratic displacement polynomial coefficients
// for contact (vin, vout) with normal vector u, scaled by chart signs s = (sx, sy, sz).
inline void AccumulateContactPoly(double C[10], double weight, vec3 u,
                                  vec3 vin, vec3 vout, vec3 s) {
  double sx = s.x, sy = s.y, sz = s.z;
  double ux = u.x, uy = u.y, uz = u.z;
  double vx = vin.x, vy = vin.y, vz = vin.z;
  double ox = vout.x, oy = vout.y, oz = vout.z;

  // Constant term (m = 0):
  C[0] += weight * (ux * (sx*vx - ox) + uy * (sy*vy - oy) + uz * (sz*vz - oz));

  // Linear terms:
  C[1] += weight * 2.0 * (-uy * sy * vz + uz * sz * vy);
  C[2] += weight * 2.0 * (ux * sx * vz - uz * sz * vx);
  C[3] += weight * 2.0 * (-ux * sx * vy + uy * sy * vx);

  // Quadratic pure terms:
  C[4] += weight * (ux * (sx*vx - ox) - uy * (sy*vy + oy) - uz * (sz*vz + oz));
  C[7] += weight * (-ux * (sx*vx + ox) + uy * (sy*vy - oy) - uz * (sz*vz + oz));
  C[9] += weight * (-ux * (sx*vx + ox) - uy * (sy*vy + oy) + uz * (sz*vz - oz));

  // Quadratic cross terms:
  C[5] += weight * 2.0 * (ux * sx * vy + uy * sy * vx);
  C[6] += weight * 2.0 * (ux * sx * vz + uz * sz * vx);
  C[8] += weight * 2.0 * (uy * sy * vz + uz * sz * vy);
}

// CPU evaluator matching Bernstein logic
static GpuResult EvaluateBoxCPU(
    const GpuBox &box,
    const std::vector<GpuContact> &contacts,
    const std::vector<GpuTriple> &triples) {
  ctr_cpu++;

  double x0 = box.cx, y0 = box.cy, z0 = box.cz;
  double num[3][3] = {
    {1.0 + x0*x0 - y0*y0 - z0*z0, 2.0*(x0*y0 - z0), 2.0*(x0*z0 + y0)},
    {2.0*(x0*y0 + z0), 1.0 - x0*x0 + y0*y0 - z0*z0, 2.0*(y0*z0 - x0)},
    {2.0*(x0*z0 - y0), 2.0*(y0*z0 + x0), 1.0 - x0*x0 - y0*y0 + z0*z0}
  };
  double denom0 = 1.0 + x0*x0 + y0*y0 + z0*z0;

  vec3 s = {
    (box.chart == 2 || box.chart == 3) ? -1.0 : 1.0,
    (box.chart == 1 || box.chart == 3) ? -1.0 : 1.0,
    (box.chart == 1 || box.chart == 2) ? -1.0 : 1.0
  };

  vec3 view_center = {
    (box.tri[0][0] + box.tri[1][0] + box.tri[2][0]) / 3.0,
    (box.tri[0][1] + box.tri[1][1] + box.tri[2][1]) / 3.0,
    (box.tri[0][2] + box.tri[1][2] + box.tri[2][2]) / 3.0
  };

  double ex = std::max(std::abs(box.cx - box.rx), std::abs(box.cx + box.rx));
  double ey = std::max(std::abs(box.cy - box.ry), std::abs(box.cy + box.ry));
  double ez = std::max(std::abs(box.cz - box.rz), std::abs(box.cz + box.rz));
  double d_bound = 1.0 + ex*ex + ey*ey + ez*ez;

  double lx = box.cx - box.rx, ly = box.cy - box.ry, lz = box.cz - box.rz;
  double wx = 2.0 * box.rx, wy = 2.0 * box.ry, wz = 2.0 * box.rz;
  double disp_error = 300.0 * d_bound * TIGHT_VERTEX_ERROR;

  vec3 p[6];
  p[0] = {box.tri[0][0], box.tri[0][1], box.tri[0][2]};
  p[1] = {box.tri[1][0], box.tri[1][1], box.tri[1][2]};
  p[2] = {box.tri[2][0], box.tri[2][1], box.tri[2][2]};
  p[3] = (p[0] + p[1]) * 0.5;
  p[4] = (p[1] + p[2]) * 0.5;
  p[5] = (p[2] + p[0]) * 0.5;

  // Pre-rotate all 20 inner vertices at box center
  vec3 rot_vin[20];
  for (int k = 0; k < NUM_VERTICES; k++) {
    vec3 vin = {VERTICES[k][0], VERTICES[k][1], VERTICES[k][2]};
    rot_vin[k] = {
      s.x * (num[0][0]*vin.x + num[0][1]*vin.y + num[0][2]*vin.z),
      s.y * (num[1][0]*vin.x + num[1][1]*vin.y + num[1][2]*vin.z),
      s.z * (num[2][0]*vin.x + num[2][1]*vin.y + num[2][2]*vin.z)
    };
  }

  // Precompute best inner vertex and unit center polynomial for each contact in this pool
  int c_start = box.contact_offset;
  int c_count = (box.num_contacts > 0) ? box.num_contacts : (int)contacts.size();
  std::vector<int> best_in(c_count);
  std::vector<std::array<double, 10>> psi_center(c_count);
  for (int c = 0; c < c_count; c++) {
    const auto &gc = contacts[c_start + c];
    vec3 edge = {gc.edge[0], gc.edge[1], gc.edge[2]};
    vec3 out = {VERTICES[gc.vertex][0], VERTICES[gc.vertex][1], VERTICES[gc.vertex][2]};
    vec3 u = yocto::cross(view_center, edge);
    double best_val = -1e30;
    int best_k = 0;
    for (int k = 0; k < NUM_VERTICES; k++) {
      vec3 disp = rot_vin[k] - denom0 * out;
      double v = yocto::dot(u, disp);
      if (v > best_val) {
        best_val = v;
        best_k = k;
      }
    }
    best_in[c] = best_k;

    vec3 vin = {VERTICES[best_k][0], VERTICES[best_k][1], VERTICES[best_k][2]};
    double poly[10] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    AccumulateContactPoly(poly, 1.0, u, vin, out, s);
    for (int m = 0; m < 10; m++) {
      psi_center[c][m] = poly[m];
    }
  }

  GpuResult res;
  res.certified = 0;
  res.winning_triple = -1;
  res.inner[0] = 0;
  res.inner[1] = 0;
  res.inner[2] = 0;
  res.margin = -1e30;

  int passed_w_positive = 0;
  int passed_center = 0;
  double best_center_margin = -1e30;
  double best_defect = 0.0;
  double best_min_b = 0.0;

  for (int t = 0; t < box.num_triples; t++) {
    const auto &trip = triples[box.triple_offset + t];

    double defect_penalty = d_bound * trip.weighted_defect_upper;

    int loc_c0 = trip.c0;
    int loc_c1 = trip.c1;
    int loc_c2 = trip.c2;
    if (loc_c0 >= c_count || loc_c1 >= c_count || loc_c2 >= c_count) continue;

    // Stage 1: Fast filter at view_center (27 controls)
    const auto &c1 = contacts[c_start + loc_c1];
    const auto &c2 = contacts[c_start + loc_c2];
    vec3 edge1 = {c1.edge[0], c1.edge[1], c1.edge[2]};
    vec3 edge2 = {c2.edge[0], c2.edge[1], c2.edge[2]};

    vec3 w_coeff0 = yocto::cross(edge1, edge2);
    double w0 = yocto::dot(view_center, w_coeff0);
    if (w0 <= 1e-9) continue;

    const auto &c0 = contacts[c_start + loc_c0];
    vec3 edge0 = {c0.edge[0], c0.edge[1], c0.edge[2]};

    vec3 w_coeff1 = yocto::cross(edge2, edge0);
    double w1 = yocto::dot(view_center, w_coeff1);
    if (w1 <= 1e-9) continue;

    vec3 w_coeff2 = yocto::cross(edge0, edge1);
    double w2 = yocto::dot(view_center, w_coeff2);
    if (w2 <= 1e-9) continue;

    passed_w_positive++;

    double C_center[10];
    for (int m = 0; m < 10; m++) {
      C_center[m] = w0 * psi_center[loc_c0][m] +
                    w1 * psi_center[loc_c1][m] +
                    w2 * psi_center[loc_c2][m];
    }

    double min_b_center = Bernstein27Min(C_center, lx, ly, lz, wx, wy, wz);
    double cmargin = min_b_center - defect_penalty - disp_error;
    if (cmargin > best_center_margin) {
      best_center_margin = cmargin;
      best_defect = defect_penalty;
      best_min_b = min_b_center;
    }
    if (cmargin <= 0.0) {
      continue;
    }
    passed_center++;

    // Stage 2: Simplex Bernstein evaluation with progressive early exit
    // Only evaluated when center filter passes (~12% of triples).
    int in0 = best_in[loc_c0];
    int in1 = best_in[loc_c1];
    int in2 = best_in[loc_c2];

    vec3 vin0 = {VERTICES[in0][0], VERTICES[in0][1], VERTICES[in0][2]};
    vec3 vout0 = {VERTICES[c0.vertex][0], VERTICES[c0.vertex][1], VERTICES[c0.vertex][2]};

    vec3 vin1 = {VERTICES[in1][0], VERTICES[in1][1], VERTICES[in1][2]};
    vec3 vout1 = {VERTICES[c1.vertex][0], VERTICES[c1.vertex][1], VERTICES[c1.vertex][2]};

    vec3 vin2 = {VERTICES[in2][0], VERTICES[in2][1], VERTICES[in2][2]};
    vec3 vout2 = {VERTICES[c2.vertex][0], VERTICES[c2.vertex][1], VERTICES[c2.vertex][2]};

    // Stage 2: Simplex Bernstein evaluation with progressive early exit
    // Corner 0
    double C0[10] = {0};
    double w0_0 = yocto::dot(p[0], w_coeff0);
    double w1_0 = yocto::dot(p[0], w_coeff1);
    double w2_0 = yocto::dot(p[0], w_coeff2);
    AccumulateContactPoly(C0, w0_0, yocto::cross(p[0], edge0), vin0, vout0, s);
    AccumulateContactPoly(C0, w1_0, yocto::cross(p[0], edge1), vin1, vout1, s);
    AccumulateContactPoly(C0, w2_0, yocto::cross(p[0], edge2), vin2, vout2, s);
    double min_0 = Bernstein27Min(C0, lx, ly, lz, wx, wy, wz);
    if (min_0 - defect_penalty - disp_error <= 0.0) continue;

    // Corner 1
    double C1[10] = {0};
    double w0_1 = yocto::dot(p[1], w_coeff0);
    double w1_1 = yocto::dot(p[1], w_coeff1);
    double w2_1 = yocto::dot(p[1], w_coeff2);
    AccumulateContactPoly(C1, w0_1, yocto::cross(p[1], edge0), vin0, vout0, s);
    AccumulateContactPoly(C1, w1_1, yocto::cross(p[1], edge1), vin1, vout1, s);
    AccumulateContactPoly(C1, w2_1, yocto::cross(p[1], edge2), vin2, vout2, s);
    double min_1 = Bernstein27Min(C1, lx, ly, lz, wx, wy, wz);
    if (min_1 - defect_penalty - disp_error <= 0.0) continue;

    // Corner 2
    double C2[10] = {0};
    double w0_2 = yocto::dot(p[2], w_coeff0);
    double w1_2 = yocto::dot(p[2], w_coeff1);
    double w2_2 = yocto::dot(p[2], w_coeff2);
    AccumulateContactPoly(C2, w0_2, yocto::cross(p[2], edge0), vin0, vout0, s);
    AccumulateContactPoly(C2, w1_2, yocto::cross(p[2], edge1), vin1, vout1, s);
    AccumulateContactPoly(C2, w2_2, yocto::cross(p[2], edge2), vin2, vout2, s);
    double min_2 = Bernstein27Min(C2, lx, ly, lz, wx, wy, wz);
    if (min_2 - defect_penalty - disp_error <= 0.0) continue;

    // Midpoint p[3] (Edge 01)
    double M[10] = {0};
    double w0_3 = yocto::dot(p[3], w_coeff0);
    double w1_3 = yocto::dot(p[3], w_coeff1);
    double w2_3 = yocto::dot(p[3], w_coeff2);
    AccumulateContactPoly(M, w0_3, yocto::cross(p[3], edge0), vin0, vout0, s);
    AccumulateContactPoly(M, w1_3, yocto::cross(p[3], edge1), vin1, vout1, s);
    AccumulateContactPoly(M, w2_3, yocto::cross(p[3], edge2), vin2, vout2, s);
    double Q[10];
    for (int m = 0; m < 10; m++) Q[m] = 2.0 * M[m] - 0.5 * (C0[m] + C1[m]);
    double min_3 = Bernstein27Min(Q, lx, ly, lz, wx, wy, wz);
    if (min_3 - defect_penalty - disp_error <= 0.0) continue;

    // Midpoint p[4] (Edge 12)
    for (int m = 0; m < 10; m++) M[m] = 0;
    double w0_4 = yocto::dot(p[4], w_coeff0);
    double w1_4 = yocto::dot(p[4], w_coeff1);
    double w2_4 = yocto::dot(p[4], w_coeff2);
    AccumulateContactPoly(M, w0_4, yocto::cross(p[4], edge0), vin0, vout0, s);
    AccumulateContactPoly(M, w1_4, yocto::cross(p[4], edge1), vin1, vout1, s);
    AccumulateContactPoly(M, w2_4, yocto::cross(p[4], edge2), vin2, vout2, s);
    for (int m = 0; m < 10; m++) Q[m] = 2.0 * M[m] - 0.5 * (C1[m] + C2[m]);
    double min_4 = Bernstein27Min(Q, lx, ly, lz, wx, wy, wz);
    if (min_4 - defect_penalty - disp_error <= 0.0) continue;

    // Midpoint p[5] (Edge 20)
    for (int m = 0; m < 10; m++) M[m] = 0;
    double w0_5 = yocto::dot(p[5], w_coeff0);
    double w1_5 = yocto::dot(p[5], w_coeff1);
    double w2_5 = yocto::dot(p[5], w_coeff2);
    AccumulateContactPoly(M, w0_5, yocto::cross(p[5], edge0), vin0, vout0, s);
    AccumulateContactPoly(M, w1_5, yocto::cross(p[5], edge1), vin1, vout1, s);
    AccumulateContactPoly(M, w2_5, yocto::cross(p[5], edge2), vin2, vout2, s);
    for (int m = 0; m < 10; m++) Q[m] = 2.0 * M[m] - 0.5 * (C2[m] + C0[m]);
    double min_5 = Bernstein27Min(Q, lx, ly, lz, wx, wy, wz);
    if (min_5 - defect_penalty - disp_error <= 0.0) continue;

    // All 6 simplex Bernstein bounds passed!
    double min_162 = std::min({min_0, min_1, min_2, min_3, min_4, min_5});
    res.certified = 1;
    res.winning_triple = t;
    res.inner[0] = in0;
    res.inner[1] = in1;
    res.inner[2] = in2;
    res.margin = min_162 - defect_penalty - disp_error;
    break;
  }

  if (!res.certified && box.rx < 1e-6) {
    Print("EvalBoxCPU failed: passed_w={}, passed_center={}, best_cmargin={:.10g} (min_b={:.10g}, defect={:.10g}, disp={:.10g})\n",
          passed_w_positive, passed_center, best_center_margin, best_min_b, best_defect, disp_error);
  }

  return res;
}

// Solution witness holding outer and inner frames and confirmed clearance.
struct SolutionWitness {
  frame3 outer_frame;
  frame3 inner_frame;
  double clearance = 0.0;
};

static Polyhedron GetPolyhedron229() {
  Polyhedron poly;
  poly.name = "nopert_229";
  poly.vertices.reserve(NUM_VERTICES);
  for (int i = 0; i < NUM_VERTICES; i++) {
    poly.vertices.push_back(vec3{
        VERTICES[i][0],
        VERTICES[i][1],
        VERTICES[i][2],
      });
  }
  return poly;
}

// Check whether a leaf SearchNode contains a valid Rupert passage.
// Converts the projective view and Cayley box into 3D frames, solves
// for the optimal 2D translation via MaximizeClearance2D, and
// formally verifies positive clearance using GetClearance.
static std::optional<SolutionWitness> CheckSolutionWitness(
    const SearchNode &node) {
  static const Polyhedron poly = GetPolyhedron229();

  // Test multiple view directions within the leaf triangle
  // (centroid + 3 corners).
  std::vector<vec3> view_candidates = {
    node.tri.Centroid(),
    node.tri.corners[0],
    node.tri.corners[1],
    node.tri.corners[2]
  };

  // Chart sign reflections for Cayley coordinates
  double sx = 1.0, sy = 1.0, sz = 1.0;
  if (node.chart == 1) { sy = -1.0; sz = -1.0; }
  else if (node.chart == 2) { sx = -1.0; sz = -1.0; }

  vec3 center_w = {
    sx * node.box.center.x,
    sy * node.box.center.y,
    sz * node.box.center.z,
  };

  // Candidate Cayley rotation vectors to test (center + box corners)
  std::vector<vec3> rot_candidates = {center_w};
  double rx = node.box.radii.x, ry = node.box.radii.y, rz = node.box.radii.z;
  for (double dx : {-rx, rx}) {
    for (double dy : {-ry, ry}) {
      for (double dz : {-rz, rz}) {
        rot_candidates.push_back(center_w + vec3{dx, dy, dz});
      }
    }
  }

  for (const vec3 &raw_view : view_candidates) {
    vec3 view = yocto::normalize(raw_view);
    vec3 right;
    if (std::abs(view.z) < 0.9) {
      right = yocto::normalize(yocto::cross(view, vec3{0, 0, 1}));
    } else {
      right = yocto::normalize(yocto::cross(view, vec3{1, 0, 0}));
    }
    vec3 up = yocto::cross(view, right);

    frame3 outer_frame;
    outer_frame.x = vec3{right.x, up.x, view.x};
    outer_frame.y = vec3{right.y, up.y, view.y};
    outer_frame.z = vec3{right.z, up.z, view.z};
    outer_frame.o = vec3{0, 0, 0};

    std::vector<vec2> outer_verts(NUM_VERTICES);
    for (int i = 0; i < NUM_VERTICES; i++) {
      vec3 v = {VERTICES[i][0], VERTICES[i][1], VERTICES[i][2]};
      outer_verts[i] = vec2{yocto::dot(right, v), yocto::dot(up, v)};
    }
    std::vector<int> outer_hull = Hull2D::QuickHull(outer_verts);
    if (outer_hull.size() < 3) continue;
    auto outer_edges = GetHullEdges(outer_verts, outer_hull);

    auto EvalClearance = [&](const vec3 &rot_w, Clearance2D *out_c2d,
                             frame3 *out_inner) -> double {
      double r2 = rot_w.x * rot_w.x + rot_w.y * rot_w.y + rot_w.z * rot_w.z;
      double denom = 1.0 + r2;
      double R[3][3] = {
        { (1.0 + rot_w.x*rot_w.x - rot_w.y*rot_w.y - rot_w.z*rot_w.z) / denom,
          2.0 * (rot_w.x*rot_w.y - rot_w.z) / denom,
          2.0 * (rot_w.x*rot_w.z + rot_w.y) / denom },
        { 2.0 * (rot_w.y*rot_w.x + rot_w.z) / denom,
          (1.0 - rot_w.x*rot_w.x + rot_w.y*rot_w.y - rot_w.z*rot_w.z) / denom,
          2.0 * (rot_w.y*rot_w.z - rot_w.x) / denom },
        { 2.0 * (rot_w.z*rot_w.x - rot_w.y) / denom,
          2.0 * (rot_w.z*rot_w.y + rot_w.x) / denom,
          (1.0 - rot_w.x*rot_w.x - rot_w.y*rot_w.y + rot_w.z*rot_w.z) / denom }
      };

      std::vector<vec2> inner_verts(20);
      for (int i = 0; i < 20; i++) {
        vec3 v = {VERTICES[i][0], VERTICES[i][1], VERTICES[i][2]};
        vec3 rv = {
          R[0][0]*v.x + R[0][1]*v.y + R[0][2]*v.z,
          R[1][0]*v.x + R[1][1]*v.y + R[1][2]*v.z,
          R[2][0]*v.x + R[2][1]*v.y + R[2][2]*v.z
        };
        inner_verts[i] = vec2{yocto::dot(right, rv), yocto::dot(up, rv)};
      }

      Clearance2D c2d = MaximizeClearance2D(outer_edges, inner_verts);
      if (out_c2d)
        *out_c2d = c2d;
      if (out_inner) {
        out_inner->x = outer_frame.x * R[0][0] + outer_frame.y * R[1][0] +
                       outer_frame.z * R[2][0];
        out_inner->y = outer_frame.x * R[0][1] + outer_frame.y * R[1][1] +
                       outer_frame.z * R[2][1];
        out_inner->z = outer_frame.x * R[0][2] + outer_frame.y * R[1][2] +
                       outer_frame.z * R[2][2];
        out_inner->o = vec3{c2d.translation.x, c2d.translation.y, 0.0};
      }
      return c2d.clearance;
    };

    // First test candidates directly
    for (const vec3 &rot_w : rot_candidates) {
      Clearance2D c2d;
      frame3 inner_f;
      double c = EvalClearance(rot_w, &c2d, &inner_f);
      if (c > 0.0) {
        auto verified = GetClearance(poly, outer_frame, inner_f);
        if (verified && *verified > 0.0) {
          return SolutionWitness{outer_frame, inner_f, *verified};
        }
      }
    }

    // Fast 3D Nelder-Mead simplex inside the leaf box to locate
    // interior maximum.
    vec3 p[4] = {
      center_w,
      center_w + vec3{rx * 0.5, 0, 0},
      center_w + vec3{0, ry * 0.5, 0},
      center_w + vec3{0, 0, rz * 0.5}
    };
    double val[4];
    for (int i = 0; i < 4; i++) {
      Clearance2D c2d;
      frame3 inner_f;
      val[i] = EvalClearance(p[i], &c2d, &inner_f);
      if (val[i] > 0.0) {
        auto verified = GetClearance(poly, outer_frame, inner_f);
        if (verified && *verified > 0.0) {
          return SolutionWitness{outer_frame, inner_f, *verified};
        }
      }
    }

    for (int iter = 0; iter < 30; iter++) {
      for (int i = 0; i < 4; i++) {
        for (int j = i + 1; j < 4; j++) {
          if (val[j] > val[i]) {
            std::swap(val[i], val[j]);
            std::swap(p[i], p[j]);
          }
        }
      }
      if (val[0] > 0.0) {
        Clearance2D c2d;
        frame3 inner_f;
        EvalClearance(p[0], &c2d, &inner_f);
        auto verified = GetClearance(poly, outer_frame, inner_f);
        if (verified && *verified > 0.0) {
          return SolutionWitness{outer_frame, inner_f, *verified};
        }
      }

      vec3 x0 = (p[0] + p[1] + p[2]) * (1.0 / 3.0);
      vec3 xr = x0 + (x0 - p[3]);
      Clearance2D c2d_r;
      frame3 inner_r;
      double vr = EvalClearance(xr, &c2d_r, &inner_r);
      if (vr > 0.0) {
        auto verified = GetClearance(poly, outer_frame, inner_r);
        if (verified && *verified > 0.0)
          return SolutionWitness{outer_frame, inner_r, *verified};
      }

      if (vr > val[0]) {
        vec3 xe = x0 + (xr - x0) * 2.0;
        Clearance2D c2d_e;
        frame3 inner_e;
        double ve = EvalClearance(xe, &c2d_e, &inner_e);
        if (ve > 0.0) {
          auto verified = GetClearance(poly, outer_frame, inner_e);
          if (verified && *verified > 0.0)
            return SolutionWitness{outer_frame, inner_e, *verified};
        }
        if (ve > vr) { p[3] = xe; val[3] = ve; }
        else { p[3] = xr; val[3] = vr; }
      } else if (vr > val[2]) {
        p[3] = xr; val[3] = vr;
      } else {
        vec3 xc = x0 + (p[3] - x0) * 0.5;
        Clearance2D c2d_c;
        frame3 inner_c;
        double vc = EvalClearance(xc, &c2d_c, &inner_c);
        if (vc > 0.0) {
          auto verified = GetClearance(poly, outer_frame, inner_c);
          if (verified && *verified > 0.0)
            return SolutionWitness{outer_frame, inner_c, *verified};
        }
        if (vc > val[3]) { p[3] = xc; val[3] = vc; }
        else {
          for (int i = 1; i < 4; i++) {
            p[i] = p[0] + (p[i] - p[0]) * 0.5;
            val[i] = EvalClearance(p[i], nullptr, nullptr);
          }
        }
      }
    }
  }

  return std::nullopt;
}

// Checkpoint metadata
struct CheckpointHeader {
  static constexpr uint64_t kMagic = 0x4E4F504552543232ULL; // "NOPERT22"
  static constexpr int32_t kVersion = 5;

  uint64_t magic = kMagic;
  int32_t version = kVersion;
  int32_t chart = 0;
  int64_t next_node_id = 0;
  int64_t evaluated_count = 0;
  int64_t certified_count = 0;
  int64_t pruned_count = 0;
  int64_t split_count = 0;
  uint64_t stack_size = 0;
  int64_t difficult_count = 0;
};

// Search manager running branch-and-bound
struct SearchManager {
  int chart = 0;
  int batch_size = 32768;
  int max_depth = 80;
  int max_box_depth = 60;
  int max_view_depth = 20;
  int suspicious_depth = 70;
  size_t num_candidates = 0; // 0 = all
  int cone_samples = 8;
  int escalate_depth = 36;
  int escalate_cone_samples = 12;
  int deep_escalate_depth = 60;
  int deep_escalate_cone_samples = 14;
  int num_threads = 8;

  int EffectiveConeSamples(const SearchNode &node) const {
    if (deep_escalate_depth > 0 && node.depth >= deep_escalate_depth) {
      return deep_escalate_cone_samples;
    }
    if (escalate_depth > 0 && node.depth >= escalate_depth) {
      return escalate_cone_samples;
    }
    return cone_samples;
  }
  bool use_gpu = true;
  bool resume = true;
  bool prioritize_related = true;
  double related_epsilon = 0.05;
  double tube_radius = 1e-4;
  double split_kappa = 1.0;
  std::string output_dir = ".artifacts/nopert229";

  std::vector<SearchNode> stack; // DFS LIFO stack
  int64_t next_node_id = 0;

  static constexpr int kMaxTrackDepth = 128;
  static constexpr int kMaxTrackK = 256;

  std::atomic<uint64_t> cert_by_depth[kMaxTrackDepth]{};
  std::atomic<uint64_t> cert_by_box_depth[kMaxTrackDepth]{};
  std::atomic<uint64_t> cert_by_view_depth[64]{};

  std::unique_ptr<std::atomic<uint64_t>[]> cert_k_by_depth;
  std::unique_ptr<std::atomic<uint64_t>[]> cert_k_by_box_depth;
  std::unique_ptr<std::atomic<uint64_t>[]> cert_k_by_view_depth;

  SearchManager() {
    cert_k_by_depth = std::make_unique<std::atomic<uint64_t>[]>(kMaxTrackDepth * kMaxTrackK);
    cert_k_by_box_depth = std::make_unique<std::atomic<uint64_t>[]>(kMaxTrackDepth * kMaxTrackK);
    cert_k_by_view_depth = std::make_unique<std::atomic<uint64_t>[]>(64 * kMaxTrackK);
    for (size_t i = 0; i < kMaxTrackDepth * kMaxTrackK; i++) {
      cert_k_by_depth[i].store(0, std::memory_order_relaxed);
      cert_k_by_box_depth[i].store(0, std::memory_order_relaxed);
    }
    for (size_t i = 0; i < 64 * kMaxTrackK; i++) {
      cert_k_by_view_depth[i].store(0, std::memory_order_relaxed);
    }
  }

  static double EvalKVolume(const std::atomic<uint64_t> *k_table, int row, int num_k = kMaxTrackK) {
    if (!k_table) return 0.0;
    double vol = 0.0;
    for (int k = num_k - 1; k > 0; k--) {
      uint64_t c = k_table[row * num_k + k].load(std::memory_order_relaxed);
      vol = (vol + c) * 0.5;
    }
    vol += k_table[row * num_k + 0].load(std::memory_order_relaxed);
    return vol;
  }

  void RecordCertification(const SearchNode &node) {
    certified_count++;
    int k = node.box_depth + 2 * node.view_depth;
    if (node.depth < kMaxTrackDepth) {
      cert_by_depth[node.depth].fetch_add(1, std::memory_order_relaxed);
      if (k < kMaxTrackK && cert_k_by_depth) {
        cert_k_by_depth[node.depth * kMaxTrackK + k].fetch_add(1, std::memory_order_relaxed);
      }
    }
    if (node.box_depth < kMaxTrackDepth) {
      cert_by_box_depth[node.box_depth].fetch_add(1, std::memory_order_relaxed);
      if (k < kMaxTrackK && cert_k_by_box_depth) {
        cert_k_by_box_depth[node.box_depth * kMaxTrackK + k].fetch_add(1, std::memory_order_relaxed);
      }
    }
    if (node.view_depth < 64) {
      cert_by_view_depth[node.view_depth].fetch_add(1, std::memory_order_relaxed);
      if (k < kMaxTrackK && cert_k_by_view_depth) {
        cert_k_by_view_depth[node.view_depth * kMaxTrackK + k].fetch_add(1, std::memory_order_relaxed);
      }
    }
  }

  std::string FormatDepthHistogram() const {
    uint64_t total = 0;
    int max_d = 0;
    uint64_t b0 = 0, b1 = 0, b2 = 0, b3 = 0; // <40, 40-69, 70-89, 90+
    for (int d = 0; d < kMaxTrackDepth; d++) {
      uint64_t cnt = cert_by_depth[d].load(std::memory_order_relaxed);
      if (cnt > 0) {
        total += cnt;
        max_d = d;
        if (d < 40) b0 += cnt;
        else if (d < 70) b1 += cnt;
        else if (d < 90) b2 += cnt;
        else b3 += cnt;
      }
    }
    if (total == 0) return AGREY("Cert depths: none yet");
    double inv = 100.0 / total;
    return std::format(
        "Cert depths: <40: {:.1f}% " AGREY("|")
        " 40-69: {:.1f}% " AGREY("|")
        " 70-89: {:.1f}% " AGREY("|")
        " 90+: {} (max {})",
        b0 * inv, b1 * inv, b2 * inv, FormatNum(b3), max_d);
  }

  void PrintDepthDistribution() {
    uint64_t total = 0;
    int max_d = 0;
    for (int d = 0; d < kMaxTrackDepth; d++) {
      uint64_t c = cert_by_depth[d].load(std::memory_order_relaxed);
      if (c > 0) {
        total += c;
        max_d = d;
      }
    }
    if (total == 0) return;

    double total_vol = 0.0;
    std::vector<double> vol_by_depth(max_d + 1, 0.0);
    for (int d = 0; d <= max_d; d++) {
      vol_by_depth[d] = EvalKVolume(cert_k_by_depth.get(), d);
      total_vol += vol_by_depth[d];
    }

    status.Print("\n" ABLUE("── Certified Depth Distribution (total: {}, 5D vol: {:.4f}%) ────────") "\n",
                 FormatNum(total), total_vol * 100.0);
    status.Print("  Depth range       Count     Count%   CumCount%         Vol%      CumVol%\n");
    uint64_t cum_count = 0;
    double cum_vol = 0.0;
    for (int start = 0; start <= max_d; start += 10) {
      int end = std::min(start + 9, max_d);
      uint64_t bucket_cnt = 0;
      double bucket_vol = 0.0;
      for (int d = start; d <= end; d++) {
        bucket_cnt += cert_by_depth[d].load(std::memory_order_relaxed);
        bucket_vol += vol_by_depth[d];
      }
      cum_count += bucket_cnt;
      cum_vol += bucket_vol;
      if (bucket_cnt > 0) {
        double count_pct = (100.0 * bucket_cnt) / total;
        double cum_count_pct = (100.0 * cum_count) / total;
        double vol_pct = bucket_vol * 100.0;
        double cum_vol_pct = cum_vol * 100.0;
        status.Print("  {:3d} .. {:3d}   {:11s}    {:6.2f}%    {:6.2f}%   {:10.4f}%   {:10.4f}%\n",
                     start, end, FormatNum(bucket_cnt), count_pct, cum_count_pct, vol_pct, cum_vol_pct);
      }
    }
    status.Print("  " AGREY("(Detailed discrete Depth, BoxDepth, and ViewDepth CDFs written to ")
                 "{}/depth_histogram.txt" AGREY(")") "\n", output_dir);
    status.Print(ABLUE("────────────────────────────────────────────────────────────────────────────") "\n\n");
  }

  void WriteDepthHistogramFile(const std::string &filename) const {
    FILE *f = fopen(filename.c_str(), "w");
    if (!f) return;
    uint64_t total_nodes = 0;
    int max_d = 0;
    for (int d = 0; d < kMaxTrackDepth; d++) {
      uint64_t c = cert_by_depth[d].load(std::memory_order_relaxed);
      if (c > 0) {
        total_nodes += c;
        max_d = d;
      }
    }

    double total_vol = 0.0;
    std::vector<double> vol_by_depth(max_d + 1, 0.0);
    for (int d = 0; d <= max_d; d++) {
      vol_by_depth[d] = EvalKVolume(cert_k_by_depth.get(), d);
      total_vol += vol_by_depth[d];
    }

    auto FormatPct = [](double pct) -> std::string {
      if (pct == 0.0) return "       0.0000%";
      if (pct >= 1e-4) return std::format("{:13.6f}%", pct);
      return std::format("{:13.4e}%", pct);
    };

    std::fprintf(f, "# Rupert 229 Proof Search - Certification Depth CDF\n");
    std::fprintf(f, "Total certified nodes: %llu\n", (unsigned long long)total_nodes);
    std::fprintf(f, "Max certified depth: %d\n", max_d);
    std::fprintf(f, "Total 5D volume certified: %s\n\n", FormatPct(total_vol * 100.0).c_str());

    std::fprintf(f, "%-7s %12s %10s %11s %16s %16s\n",
                 "Depth", "Count", "Count%", "CumCount%", "5D_Vol%", "Cum_5D_Vol%");

    uint64_t cum_count = 0;
    double cum_vol = 0.0;
    for (int d = 0; d <= max_d; d++) {
      uint64_t c = cert_by_depth[d].load(std::memory_order_relaxed);
      cum_count += c;
      cum_vol += vol_by_depth[d];
      double count_pct = total_nodes > 0 ? (100.0 * c) / total_nodes : 0.0;
      double cum_count_pct = total_nodes > 0 ? (100.0 * cum_count) / total_nodes : 0.0;
      double vol_pct = vol_by_depth[d] * 100.0;
      double cum_vol_pct = cum_vol * 100.0;

      std::fprintf(f, "%5d   %12llu   %9.4f%%   %9.4f%%   %16s   %16s\n",
                   d, (unsigned long long)c, count_pct, cum_count_pct,
                   FormatPct(vol_pct).c_str(), FormatPct(cum_vol_pct).c_str());
    }

    // Box depth discrete CDF
    int max_box_d = 0;
    uint64_t total_box_nodes = 0;
    for (int d = 0; d < kMaxTrackDepth; d++) {
      uint64_t c = cert_by_box_depth[d].load(std::memory_order_relaxed);
      if (c > 0) {
        total_box_nodes += c;
        max_box_d = d;
      }
    }
    std::fprintf(f, "\n# Box Depth CDF\n");
    std::fprintf(f, "%-10s %12s %10s %11s %16s %16s\n",
                 "BoxDepth", "Count", "Count%", "CumCount%", "5D_Vol%", "Cum_5D_Vol%");
    uint64_t cum_box_count = 0;
    double cum_box_vol = 0.0;
    for (int d = 0; d <= max_box_d; d++) {
      uint64_t c = cert_by_box_depth[d].load(std::memory_order_relaxed);
      cum_box_count += c;
      double v = EvalKVolume(cert_k_by_box_depth.get(), d);
      cum_box_vol += v;
      double count_pct = total_box_nodes > 0 ? (100.0 * c) / total_box_nodes : 0.0;
      double cum_count_pct = total_box_nodes > 0 ? (100.0 * cum_box_count) / total_box_nodes : 0.0;
      std::fprintf(f, "%8d   %12llu   %9.4f%%   %9.4f%%   %16s   %16s\n",
                   d, (unsigned long long)c, count_pct, cum_count_pct,
                   FormatPct(v * 100.0).c_str(), FormatPct(cum_box_vol * 100.0).c_str());
    }

    // View depth discrete CDF
    int max_view_d = 0;
    uint64_t total_view_nodes = 0;
    for (int d = 0; d < 64; d++) {
      uint64_t c = cert_by_view_depth[d].load(std::memory_order_relaxed);
      if (c > 0) {
        total_view_nodes += c;
        max_view_d = d;
      }
    }
    std::fprintf(f, "\n# View Depth CDF\n");
    std::fprintf(f, "%-11s %12s %10s %11s %16s %16s\n",
                 "ViewDepth", "Count", "Count%", "CumCount%", "5D_Vol%", "Cum_5D_Vol%");
    uint64_t cum_view_count = 0;
    double cum_view_vol = 0.0;
    for (int d = 0; d <= max_view_d; d++) {
      uint64_t c = cert_by_view_depth[d].load(std::memory_order_relaxed);
      cum_view_count += c;
      double v = EvalKVolume(cert_k_by_view_depth.get(), d);
      cum_view_vol += v;
      double count_pct = total_view_nodes > 0 ? (100.0 * c) / total_view_nodes : 0.0;
      double cum_count_pct = total_view_nodes > 0 ? (100.0 * cum_view_count) / total_view_nodes : 0.0;
      std::fprintf(f, "%9d   %12llu   %9.4f%%   %9.4f%%   %16s   %16s\n",
                   d, (unsigned long long)c, count_pct, cum_count_pct,
                   FormatPct(v * 100.0).c_str(), FormatPct(cum_view_vol * 100.0).c_str());
    }

    fclose(f);
  }

  // Row 0 is atomic counters only, so it can be updated any time.
  StatusBar status = StatusBar(4);
  Periodically mini_status_per = Periodically(1.0);
  std::string last_op;

  cl_program program = nullptr;
  cl_kernel kernel = nullptr;

  static std::string CLPreamble() {
    std::string s = std::format(
        "#pragma OPENCL EXTENSION cl_khr_fp64 : enable\n\n"
        "#define NUM_VERTICES {}\n\n"
        "#define MAX_CONTACTS {}\n\n"
        "#define TIGHT_VERTEX_ERROR {:.17g}\n\n"
        "__constant double VERTICES[NUM_VERTICES][3] = {{\n",
        NUM_VERTICES, MAX_GPU_CONTACTS, TIGHT_VERTEX_ERROR);
    for (int i = 0; i < NUM_VERTICES; i++) {
      s += std::format("  {{ {:.17g}, {:.17g}, {:.17g} }},\n",
                       VERTICES[i][0], VERTICES[i][1], VERTICES[i][2]);
    }
    s += "};\n\n";
    return s;
  }

  void InitOpenCL() {
    if (!use_gpu) return;
    cl = new CL(1);
    std::string kernel_src = CLPreamble() + Util::ReadFile("lean229.cl");
    Timer build_timer;
    auto [prg, k] = cl->BuildOneKernel(kernel_src, "EvaluateBoxes", 1);
    program = prg;
    kernel = k;
    status.Print(AGREEN("Built") " OpenCL kernel 'EvaluateBoxes' in {}\n",
                 ANSI::Time(build_timer.Seconds()));
  }

  bool SaveCheckpoint(const std::string &path) {
    std::string tmp = path + ".tmp";
    FILE *f = fopen(tmp.c_str(), "wb");
    if (!f) return false;

    CheckpointHeader hdr;
    hdr.chart = chart;
    hdr.next_node_id = next_node_id;
    hdr.evaluated_count = evaluated_count.Read();
    hdr.certified_count = certified_count.Read();
    hdr.pruned_count = pruned_count.Read();
    hdr.split_count = split_count.Read();
    hdr.difficult_count = difficult_count.Read();
    hdr.stack_size = stack.size();

    if (fwrite(&hdr, sizeof(hdr), 1, f) != 1) {
      fclose(f);
      return false;
    }
    if (hdr.stack_size > 0) {
      if (fwrite(stack.data(), sizeof(SearchNode), hdr.stack_size, f) !=
          hdr.stack_size) {
        fclose(f);
        return false;
      }
    }
    fclose(f);

    std::string hist_path = path + ".hist";
    FILE *hf = fopen(hist_path.c_str(), "wb");
    if (hf) {
      uint64_t raw_depth[128], raw_box[128], raw_view[64];
      for (int i = 0; i < 128; i++) raw_depth[i] = cert_by_depth[i].load(std::memory_order_relaxed);
      for (int i = 0; i < 128; i++) raw_box[i] = cert_by_box_depth[i].load(std::memory_order_relaxed);
      for (int i = 0; i < 64; i++) raw_view[i] = cert_by_view_depth[i].load(std::memory_order_relaxed);
      fwrite(raw_depth, sizeof(uint64_t), 128, hf);
      fwrite(raw_box, sizeof(uint64_t), 128, hf);
      fwrite(raw_view, sizeof(uint64_t), 64, hf);

      std::vector<uint64_t> raw_k(320 * 256);
      for (int i = 0; i < 128 * 256; i++) raw_k[i] = cert_k_by_depth[i].load(std::memory_order_relaxed);
      for (int i = 0; i < 128 * 256; i++) raw_k[128 * 256 + i] = cert_k_by_box_depth[i].load(std::memory_order_relaxed);
      for (int i = 0; i < 64 * 256; i++) raw_k[256 * 256 + i] = cert_k_by_view_depth[i].load(std::memory_order_relaxed);
      fwrite(raw_k.data(), sizeof(uint64_t), 320 * 256, hf);
      fclose(hf);
    }
    WriteDepthHistogramFile(output_dir + "/depth_histogram.txt");

    std::error_code ec;
    return std::filesystem::rename(tmp, path, ec), !ec;
  }

  bool LoadCheckpoint(const std::string &path) {
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return false;

    CheckpointHeader hdr;
    if (fread(&hdr, sizeof(hdr), 1, f) != 1) {
      status.Print("Failed to read checkpoint header from " AORANGE("{}") "\n",
                   path);
      fclose(f);
      return false;
    }
    if (hdr.magic != CheckpointHeader::kMagic) {
      status.Print("Checkpoint magic mismatch in " AORANGE("{}")
                   " (got 0x{:x}, expected 0x{:x})\n",
                   path, hdr.magic, CheckpointHeader::kMagic);
      fclose(f);
      return false;
    }
    if (hdr.version != CheckpointHeader::kVersion) {
      status.Print("Checkpoint version mismatch in " AORANGE("{}")
                   " (got {}, expected {})\n",
                   path, hdr.version, CheckpointHeader::kVersion);
      fclose(f);
      return false;
    }

    if (hdr.chart != chart) {
      status.Print("Checkpoint chart mismatch in " AORANGE("{}")
                   " (got chart {}, current chart {})\n",
                   path, hdr.chart, chart);
      fclose(f);
      return false;
    }

    next_node_id = hdr.next_node_id;
    evaluated_count.Reset(); evaluated_count += hdr.evaluated_count;
    certified_count.Reset(); certified_count += hdr.certified_count;
    pruned_count.Reset();    pruned_count += hdr.pruned_count;
    split_count.Reset();     split_count += hdr.split_count;
    difficult_count.Reset(); difficult_count += hdr.difficult_count;

    stack.resize(hdr.stack_size);
    if (hdr.stack_size > 0) {
      if (fread(stack.data(), sizeof(SearchNode), hdr.stack_size, f) !=
          hdr.stack_size) {
        status.Print("Checkpoint " AORANGE("{}")
                     " truncated while reading {} stack nodes\n",
                     path, hdr.stack_size);
        fclose(f);
        return false;
      }
    }
    fclose(f);

    std::string hist_path = path + ".hist";
    FILE *hf = fopen(hist_path.c_str(), "rb");
    if (hf) {
      uint64_t raw_depth[128], raw_box[128], raw_view[64];
      if (fread(raw_depth, sizeof(uint64_t), 128, hf) == 128 &&
          fread(raw_box, sizeof(uint64_t), 128, hf) == 128 &&
          fread(raw_view, sizeof(uint64_t), 64, hf) == 64) {
        for (int i = 0; i < 128; i++) cert_by_depth[i].store(raw_depth[i], std::memory_order_relaxed);
        for (int i = 0; i < 128; i++) cert_by_box_depth[i].store(raw_box[i], std::memory_order_relaxed);
        for (int i = 0; i < 64; i++) cert_by_view_depth[i].store(raw_view[i], std::memory_order_relaxed);

        std::vector<uint64_t> raw_k(320 * 256);
        if (fread(raw_k.data(), sizeof(uint64_t), 320 * 256, hf) == 320 * 256) {
          for (int i = 0; i < 128 * 256; i++) cert_k_by_depth[i].store(raw_k[i], std::memory_order_relaxed);
          for (int i = 0; i < 128 * 256; i++) cert_k_by_box_depth[i].store(raw_k[128 * 256 + i], std::memory_order_relaxed);
          for (int i = 0; i < 64 * 256; i++) cert_k_by_view_depth[i].store(raw_k[256 * 256 + i], std::memory_order_relaxed);
        }
      }
      fclose(hf);
    }

    return true;
  }

  struct PriorityPoint {
    vec3 view;
    vec3 w;
    int chart = 0;
    int solution_id = 0;
    std::string label;
  };
  std::vector<PriorityPoint> active_priority;
  int num_priority_points = 0;

  bool ContainsUncertifiedPriority(const SearchNode &node) const {
    if (!prioritize_related || active_priority.empty()) return false;
    for (const auto &p : active_priority) {
      if (p.chart == node.chart && node.box.Contains(p.w) &&
          node.tri.ContainsRay(p.view)) {
        return true;
      }
    }
    return false;
  }

  std::vector<PriorityPoint> GetPriorityPoints(double max_dist = 0.05) {
    std::vector<PriorityPoint> priority_points;
    // Known hard / pathological points from past runs.
    struct HardPoint {
      int chart;
      vec3 w;
      vec3 view;
      std::string_view label;
    };

    static constexpr HardPoint HARD_POINTS[] = {
        {.chart = 0,
         .w = {0.00096893310546875, -0.0012969970703125, -0.002044677734375},
         .view = {0.99999106802591464, 3.8457110645325201e-06,
                  5.0862630208333331e-06},
         .label = "Near-identity"},

        {.chart = 0,
         .w = {0.00097647309303283691, -0.0012219548225402832,
               -0.0019906759262084961},
         .view = {0.92218818586940847, 0.20787508492040183,
                  0.32612502157077433},
         .label = "Near-identity view"},

        {.chart = 0,
         .w = {0.010819882154464722, -0.016599953174591064,
               0.028645873069763184},
         .view = {0.3984395379, 0.8338690325, 0.3819795573},
         .label = "Small-rotation silhouette"},

        {.chart = 0,
         .w = {0.00030419230461120605, -0.00036638975143432617,
               -0.0005896488825480144},
         .view = {0.9164627443138856, 0.2030404322634455, 0.3443121541818299},
         .label = "Valley"},

        {.chart = 0,
         .w = {0.0003413856029510498, -0.00041025876998901367,
               -0.00066292285919189442},
         .view = {0.9164627443138856, 0.2030404322634455, 0.3443121541818299},
         .label = "Valley transition"},

        {.chart = 0,
         .w = {0.00041022896766662598, -0.00049299001693725586,
               -0.0007966756820678712},
         .view = {0.9164627443138856, 0.2030404322634455, 0.3443121541818299},
         .label = "Valley depth-out"},

        {.chart = 2,
         .w = {-0.28256338834762573, -0.86962884664535522,
               -0.27563470602035522},
         .view = {0.18209315363953751, 0.30716461912403265,
                  0.51074222723642981},
         .label = "Boundary"},

        {.chart = 2,
         .w = {1.0 / 6.0, 3.0 / 8.0, 3.0 / 4.0},
         .view = {0.7073170731707317, 0.12601626016260162, 0.16666666666666666},
         .label = "Box 326 (simplex boundary)"},

        {.chart = 2,
         .w = {-1.0 / 24.0, -13.0 / 16.0, -9.0 / 16.0},
         .view = {0.7073170731707317, 0.12601626016260162, 0.16666666666666666},
         .label = "Node 290 (verified Lean certificate)"},
    };

    int hard_count = 0;
    for (const auto &hp : HARD_POINTS) {
      if (hp.chart == chart) {
        priority_points.push_back(PriorityPoint{
            .view = hp.view,
            .w = hp.w,
            .chart = hp.chart,
            .solution_id = -1,
            .label = std::string(hp.label),
        });
        hard_count++;
      }
    }

    if (hard_count > 0) {
      status.Print("Loaded " ACYAN("{}")
                   " static hard point(s) in Chart {}.\n",
                   hard_count, chart);
    }

    if (chart == 0) {
      SolutionDB db;
      Polyhedron target_poly = db.AnyPolyhedronByName("nopert_229");
      auto solutions = db.GetRelatedSolutions(target_poly, max_dist);
      if (solutions.empty()) {
        status.Print(AYELLOW("No related solutions")
                     " within max_dist = {:.17g}.\n", max_dist);
      } else {
        status.Print("Found " ACYAN("{}") " related solutions "
                     "(max_dist = {:.17g}).\n",
                     solutions.size(), max_dist);

        for (const auto &sol : solutions) {
          const frame3 &o_frame = sol.outer_frame;
          const frame3 &i_frame = sol.inner_frame;

          vec3 sol_view =
              yocto::normalize(vec3{o_frame.x.z, o_frame.y.z, o_frame.z.z});

          vec3 out_cols[3] = {o_frame.x, o_frame.y, o_frame.z};
          vec3 inn_cols[3] = {i_frame.x, i_frame.y, i_frame.z};
          double R[3][3];
          for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
              R[i][j] = yocto::dot(out_cols[i], inn_cols[j]);
            }
          }
          double denom = 1.0 + R[0][0] + R[1][1] + R[2][2];
          if (std::abs(denom) > 1e-6) {
            vec3 sol_w = {
              (R[2][1] - R[1][2]) / denom,
              (R[0][2] - R[2][0]) / denom,
              (R[1][0] - R[0][1]) / denom
            };

            // Check all 5-fold rotations around z to find representation
            // in chart 0 wedge.
            for (int k = 0; k < 5; k++) {
              double theta = 2.0 * std::numbers::pi * k / 5.0;
              double c = std::cos(theta), s = std::sin(theta);
              vec3 rot_v = {c * sol_view.x - s * sol_view.y,
                            s * sol_view.x + c * sol_view.y, sol_view.z};
              ProjectiveTriangle wedge_root;
              wedge_root.corners[0] = {1.0, 0.0, 0.0};
              wedge_root.corners[1] = {10.0 / 41.0, 31.0 / 41.0, 0.0};
              wedge_root.corners[2] = {0.0, 0.0, 1.0};
              if (wedge_root.ContainsRay(rot_v) &&
                  std::abs(sol_w.z) <= 1.0 / 3.0 + 1e-6) {
                priority_points.push_back(PriorityPoint{
                    .view = rot_v,
                    .w = sol_w,
                    .chart = 0,
                    .solution_id = sol.id,
                    .label = "solution",
                });
                break;
              }
            }
          }
        }
        status.Print("Loaded " ACYAN("{}") " candidate solution poses "
                     "in Chart 0 (max_euclidean_dist = {}).\n",
                     priority_points.size() - hard_count, max_dist);
      }
    }
    active_priority = priority_points;
    num_priority_points = (int)priority_points.size();
    return priority_points;
  }

  // Prunes priority points that are not contained in any remaining node on the
  // stack (e.g. points that were already certified and eliminated prior to a
  // checkpoint, or outside the initial domain).
  void FilterPriorityPointsToStack() {
    if (!prioritize_related || active_priority.empty() || stack.empty()) return;
    int initial_count = (int)active_priority.size();
    std::vector<PriorityPoint> remaining;
    remaining.reserve(active_priority.size());

    for (const auto &p : active_priority) {
      bool found = false;
      for (const auto &node : stack) {
        if (p.chart == node.chart &&
            node.box.Contains(p.w) &&
            node.tri.ContainsRay(p.view)) {
          found = true;
          break;
        }
      }
      if (found) {
        remaining.push_back(p);
      }
    }

    int removed = initial_count - (int)remaining.size();
    if (removed > 0) {
      status.Print(
          "Filtered out " ACYAN("{}") " priority point(s) not in remaining volume "
          "({} active remaining in stack).\n",
          removed, remaining.size());
    }
    active_priority = std::move(remaining);
    num_priority_points = (int)active_priority.size();
  }

  // Computes exact completed domain volume fraction in [0.0, 1.0] by
  // evaluating uncertified leaf weights on the stack using depth-bucketed
  // Horner evaluation. Zero floating-point accumulation error.
  double CompletedFraction() const {
    uint64_t stack_k[256] = {0};
    for (const auto &node : stack) {
      int k = node.box_depth + 2 * node.view_depth;
      if (k < 256) stack_k[k]++;
    }
    double pending_vol = 0.0;
    for (int k = 255; k > 0; k--) {
      pending_vol = (pending_vol + stack_k[k]) * 0.5;
    }
    pending_vol += stack_k[0];
    return std::clamp(1.0 - pending_vol, 0.0, 1.0);
  }

  void InitRoot() {
    stack.clear();
    next_node_id = 0;
    evaluated_count.Reset();
    certified_count.Reset();
    pruned_count.Reset();
    split_count.Reset();
    for (int i = 0; i < 128; i++) cert_by_depth[i].store(0, std::memory_order_relaxed);
    for (int i = 0; i < 128; i++) cert_by_box_depth[i].store(0, std::memory_order_relaxed);
    for (int i = 0; i < 64; i++) cert_by_view_depth[i].store(0, std::memory_order_relaxed);
    if (cert_k_by_depth) {
      for (size_t i = 0; i < 128 * 256; i++) {
        cert_k_by_depth[i].store(0, std::memory_order_relaxed);
        cert_k_by_box_depth[i].store(0, std::memory_order_relaxed);
      }
      for (size_t i = 0; i < 64 * 256; i++) {
        cert_k_by_view_depth[i].store(0, std::memory_order_relaxed);
      }
    }

    SearchNode root;
    root.id = next_node_id++;
    root.parent_id = -1;
    root.chart = chart;
    root.box.center = {0.0, 0.0, 0.0};

    if (chart == 0) {
      // 5-fold fundamental domain: z in [-1/3, 1/3]
      root.box.radii = {1.0, 1.0, 1.0 / 3.0};
    } else if (chart == 1) {
      // 5-fold fundamental domain: y in [-1/3, 1/3]
      root.box.radii = {1.0, 1.0 / 3.0, 1.0};
    } else if (chart == 2) {
      // 5-fold fundamental domain: x in [-1/3, 1/3]
      root.box.radii = {1.0 / 3.0, 1.0, 1.0};
    } else {
      root.box.radii = {1.0, 1.0, 1.0};
    }

    // Root projective view is UPPER_WEDGE_PROJECTIVE_ROOT, pre-split
    // into 4 sub-wedges.
    ProjectiveTriangle wedge_root;
    wedge_root.corners[0] = {1.0, 0.0, 0.0};
    wedge_root.corners[1] = {10.0 / 41.0, 31.0 / 41.0, 0.0};
    wedge_root.corners[2] = {0.0, 0.0, 1.0};
    auto sub_wedges = wedge_root.Subdivide();

    int priority_idx = -1;
    for (int i = 0; i < 4; i++) {
      SearchNode child = root;
      child.tri = sub_wedges[i];
      if (ContainsUncertifiedPriority(child)) {
        priority_idx = i;
        break;
      }
    }

    SearchNode root_children[4];
    for (int i = 0; i < 4; i++) {
      root_children[i] = root;
      root_children[i].id = next_node_id++;
      root_children[i].parent_id = root.id;
      root_children[i].view_depth = 1;
      root_children[i].depth = 1;
      root_children[i].tri = sub_wedges[i];
    }

    for (int i = 0; i < 4; i++) {
      if (i == priority_idx) continue;
      stack.push_back(root_children[i]);
    }
    if (priority_idx >= 0) {
      stack.push_back(root_children[priority_idx]);
    }
  }

  std::mutex row_mutex;
  std::string row_buffer;
  FILE *row_file = nullptr;
  FILE *difficult_file = nullptr;
  std::string difficult_path;
  void FlushRowsWithLock() {
    Print(row_file, "{}", row_buffer);
    row_buffer.clear();
  }

  void FlushRows() {
    MutexLock ml(&row_mutex);
    FlushRowsWithLock();
  }

  void OutputRow(std::string_view row) {
    MutexLock ml(&row_mutex);
    row_buffer.append(row);
    if (row_buffer.size() > 32768) {
      FlushRowsWithLock();
    }
  }

  void Run() {
    std::filesystem::create_directories(output_dir);
    std::string log_path =
        std::format("{}/chart{}.rows.log", output_dir, chart);
    std::string ckpt_path =
        std::format("{}/chart{}.checkpoint.bin", output_dir, chart);
    difficult_path =
        std::format("{}/chart{}.difficult", output_dir, chart);

    status.Print(ACYAN("=== Nopert #229 Proof Search ===\n"));
    if (deep_escalate_depth > 0) {
      status.Print("Chart: {}, Batch Size: {}, Candidates: {}, Cone Samples: {} (escalate to {} at depth {}, {} at depth {}), "
                   "Max Depth: {} (box={}, view={}), Device: {}\n",
                   chart, batch_size,
                   num_candidates == 0 ? "all" : std::to_string(num_candidates),
                   cone_samples, escalate_cone_samples, escalate_depth,
                   deep_escalate_cone_samples, deep_escalate_depth,
                   max_depth, max_box_depth, max_view_depth,
                   use_gpu ? "OpenCL (GPU/CPU)" : "Multi-threaded CPU");
    } else {
      status.Print("Chart: {}, Batch Size: {}, Candidates: {}, Cone Samples: {} (escalate to {} at depth {}), "
                   "Max Depth: {} (box={}, view={}), Device: {}\n",
                   chart, batch_size,
                   num_candidates == 0 ? "all" : std::to_string(num_candidates),
                   cone_samples, escalate_cone_samples, escalate_depth,
                   max_depth, max_box_depth, max_view_depth,
                   use_gpu ? "OpenCL (GPU/CPU)" : "Multi-threaded CPU");
    }

    if (prioritize_related) {
      active_priority = GetPriorityPoints(related_epsilon);
    }

    bool resumed = false;
    if (stack.empty()) {
      if (resume && std::filesystem::exists(ckpt_path)) {
        if (LoadCheckpoint(ckpt_path)) {
          resumed = true;
          status.Print(AGREEN("Resumed")
                       " from checkpoint: {} pending nodes on "
                       "stack, {} evaluated, {} certified, {} difficult, {} pruned\n",
                       FormatNum(stack.size()),
                       FormatNum(evaluated_count.Read()),
                       FormatNum(certified_count.Read()),
                       FormatNum(difficult_count.Read()),
                       FormatNum(pruned_count.Read()));
        } else {
          status.Print(
              "Failed to read checkpoint " AORANGE("{}")
              "; starting fresh.\n",
              ckpt_path);
          std::error_code ec;
          std::filesystem::remove(ckpt_path, ec);
          InitRoot();
        }
      } else {
        if (!resume && std::filesystem::exists(ckpt_path)) {
          std::error_code ec;
          std::filesystem::remove(ckpt_path, ec);
        }
        InitRoot();
      }
    }

    if (prioritize_related) {
      FilterPriorityPointsToStack();
    }

    if (resumed) {
      row_file = fopen(log_path.c_str(), "a");
      CHECK(row_file) << log_path;
      status.Print("Appending log to: {}\n", log_path);

      if (std::filesystem::exists(difficult_path)) {
        difficult_file = fopen(difficult_path.c_str(), "a");
      } else {
        difficult_file = fopen(difficult_path.c_str(), "w");
        if (difficult_file) {
          Print(difficult_file,
                "# id parent_id depth box_depth view_depth chart "
                "cx cy cz rx ry rz "
                "v0x v0y v0z v1x v1y v1z v2x v2y v2z "
                "best_margin\n");
          std::fflush(difficult_file);
        }
      }
      CHECK(difficult_file) << difficult_path;
      status.Print("Difficult cells file: {}\n", difficult_path);
    } else {
      std::error_code ec;
      std::filesystem::remove(log_path, ec);
      row_file = fopen(log_path.c_str(), "w");
      CHECK(row_file) << log_path;
      status.Print("Writing fresh log to: {}\n", log_path);
      OutputRow("SV 0 -1 0 1 2 3 4\n");

      std::filesystem::remove(difficult_path, ec);
      difficult_file = fopen(difficult_path.c_str(), "w");
      CHECK(difficult_file) << difficult_path;
      Print(difficult_file,
            "# id parent_id depth box_depth view_depth chart "
            "cx cy cz rx ry rz "
            "v0x v0y v0z v1x v1y v1z v2x v2y v2z "
            "best_margin\n");
      std::fflush(difficult_file);
      status.Print("Writing fresh difficult cells to: {}\n", difficult_path);
    }

    Timer timer;
    Periodically progress_per(1.0);
    Periodically checkpoint_per(60.0);
    Periodically where_per(120.0);
    Periodically suspicious_per(30.0);
    Periodically depth_out_per(10.0);

    // Buffers for GPU batch
    std::vector<GpuBox> gpu_boxes;
    std::vector<GpuContact> all_contacts;
    std::vector<GpuTriple> all_triples;
    std::vector<GpuResult> results;
    std::vector<SearchNode> current_batch;

    while (!stack.empty() && !sigint_received.load()) {
      MaybeMiniStatus("stack");
      ctr_loops++;
      const size_t target_pool_size = (size_t)batch_size * 2;
      int count = 0;
      current_batch.clear();

      // First, extract any uncertified priority nodes into current_batch
      if (prioritize_related && !active_priority.empty()) {
        for (int i = (int)stack.size() - 1;
             i >= 0 && current_batch.size() < (size_t)batch_size; i--) {
          if (ContainsUncertifiedPriority(stack[i])) {
            current_batch.push_back(stack[i]);
            stack[i] = std::move(stack.back());
            stack.pop_back();
            if (i < (int)stack.size()) {
              i++; // Re-check slot i with the swapped-in element
            }
          }
        }
      }

      size_t remaining = batch_size - current_batch.size();
      if (stack.size() > remaining) {
        if (stack.size() < target_pool_size) {
          // Frontier not yet wide enough to keep GPU saturated.
          // Expand shallowest nodes to branch out the frontier!
          std::nth_element(stack.begin(), stack.begin() + remaining,
                           stack.end(),
                           [](const SearchNode &a, const SearchNode &b) {
                             return a.depth < b.depth;
                           });
          current_batch.insert(current_batch.end(), stack.begin(),
                               stack.begin() + remaining);
          stack.erase(stack.begin(), stack.begin() + remaining);
        } else {
          // Pure DFS: take directly from back of stack (LIFO).
          // Crucial: do NOT run std::nth_element here! Running
          // std::nth_element scrambles the stack order and destroys
          // spatial locality, mixing nodes from thousands of open
          // branches and exploding unique view triangles. Directly
          // taking from the back keeps all nodes in the same local
          // branch and sharing the SAME view triangle, certifying and
          // closing branches quickly.
          size_t pop_count = remaining;
          current_batch.insert(current_batch.end(), stack.end() - pop_count,
                               stack.end());
          stack.erase(stack.end() - pop_count, stack.end());
        }
      } else {
        current_batch.insert(current_batch.end(), stack.begin(), stack.end());
        stack.clear();
      }
      count = current_batch.size();

      gpu_boxes.clear();
      all_contacts.clear();
      all_triples.clear();
      results.assign(count, GpuResult{});

      // Periodically show the deepest node from the batch, so we can
      // note places where we got stuck, etc.
      where_per.RunIf([&]{
          if (current_batch.empty()) return;
          double completed_pct = 100.0 * CompletedFraction();
          size_t best_idx = 0;
          for (size_t i = 1; i < current_batch.size(); i++) {
            if (current_batch[i].depth > current_batch[best_idx].depth) {
              best_idx = i;
            }
          }
          const auto &node = current_batch[best_idx];
          status.Print("[{}] {:.5f}%  |  Deepest in batch: " ACYAN("#{}") "\n"
                       "  depth {}, box_depth {}, view_depth {}\n"
                       "  radii ({:.17g}, {:.17g}, {:.17g}))\n"
                       "  box ({:.17g}, {:.17g}, {:.17g})\n",
                       ANSI::Time(timer.Seconds()),
                       completed_pct,
                       node.id,
                       node.depth, node.box_depth, node.view_depth,
                       node.box.radii.x, node.box.radii.y, node.box.radii.z,
                       node.box.center.x, node.box.center.y, node.box.center.z);
        });

      // Mutex guards the stack and eval indices.
      std::mutex mu;
      std::vector<int> eval_indices;
      eval_indices.reserve(count);

      ParallelComp(count, [&](int64_t i) {
          const auto &node = current_batch[i];
          if (OutsideBall(node.box)) {
            pruned_count++;
            RecordCertification(node);
            OutputRow(std::format("PR {} {} {} RADIUS\n",
                                  node.id, node.parent_id, node.depth));
            return;
          }

          FundamentalPruneResult fund =
            CheckFundamentalPrune(node.chart, node.box);

          if (fund.prune) {
            pruned_count++;
            RecordCertification(node);
            OutputRow(std::format("PR {} {} {} FUNDAMENTAL {}\n",
                                  node.id, node.parent_id, node.depth,
                                  fund.direction));

          } else if (InsideIdentityTube(node.chart, node.box, tube_radius)) {
            pruned_count++;
            RecordCertification(node);
            OutputRow(std::format("TU {} {} {} {:.17g}\n",
                                  node.id, node.parent_id, node.depth,
                                  tube_radius));

          } else if (node.chart == 0 && node.box.ContainsOrigin()) {
            int widest = node.box.WidestAxis();
            auto [b0, b1] = node.box.Split(widest);

            SearchNode child0 = node;
            child0.id = next_node_id++;
            child0.parent_id = node.id;
            child0.depth = node.depth + 1;
            child0.box_depth = node.box_depth + 1;
            child0.box_splits_since_view = node.box_splits_since_view + 1;
            child0.box = b0;

            SearchNode child1 = node;
            child1.id = next_node_id++;
            child1.parent_id = node.id;
            child1.depth = node.depth + 1;
            child1.box_depth = node.box_depth + 1;
            child1.box_splits_since_view = node.box_splits_since_view + 1;
            child1.box = b1;

            split_count++;
            OutputRow(std::format("SO {} {} {} {} {}\n",
                                  node.id, node.parent_id, node.depth,
                                  child0.id, child1.id));

            if (child0.box.ContainsOrigin()) {
              MutexLock ml(&mu);
              stack.push_back(child1);
              stack.push_back(child0);
            } else {
              MutexLock ml(&mu);
              stack.push_back(child0);
              stack.push_back(child1);
            }

          } else {
            auto tpool = GetTrianglePool(node.tri, EffectiveConeSamples(node));
            if (tpool->gpu_triples.empty()) {
              split_count++;
              auto sub_tris = node.tri.Subdivide();
              int64_t child_ids[4] = {-1, -1, -1, -1};
              for (int t = sub_tris.size() - 1; t >= 0; t--) {
                SearchNode child = node;
                child.id = next_node_id++;
                child.parent_id = node.id;
                child.depth = node.depth + 1;
                child.view_depth = node.view_depth + 1;
                child.box_splits_since_view = 0;
                child.tri = sub_tris[t];
                child_ids[t] = child.id;
                MutexLock ml(&mu);
                stack.push_back(child);
              }
              OutputRow(std::format("SV {} {} {} {} {} {} {}\n", node.id,
                                    node.parent_id, node.depth,
                                    child_ids[0], child_ids[1], child_ids[2], child_ids[3]));
            } else {
              MutexLock ml(&mu);
              eval_indices.push_back(i);
            }
          }
        }, num_threads);

      if (!eval_indices.empty()) {
        struct EvalItem {
          int batch_idx;
          std::shared_ptr<const TrianglePool> pool;
        };
        std::vector<EvalItem> eval_items(eval_indices.size());
        ParallelComp(eval_indices.size(), [&](int64_t i) {
          int idx = eval_indices[i];
          const auto &node = current_batch[idx];
          eval_items[i] = {idx, GetTrianglePool(node.tri, EffectiveConeSamples(node))};
          MaybeMiniStatus("tris");
          }, num_threads);

        // Collate by triangle pool ID: groups identical triangles together so GPU work-items
        // within every warp execute in lockstep on identical candidate triples, with 100%
        // broadcast cache hits and zero memory divergence.
        MaybeMiniStatus("sort");
        std::sort(eval_items.begin(), eval_items.end(),
                  [](const EvalItem &a, const EvalItem &b) {
                    return a.pool->id < b.pool->id;
                  });

        static constexpr size_t MAX_POOLS_PER_DISPATCH = 256;
        static constexpr size_t MAX_TRIPLES_PER_DISPATCH = 32 * 1024 * 1024;
        size_t item_start = 0;
        while (item_start < eval_items.size()) {
          // Pass 1: Identify slice [item_start, item_end) and compute exact buffer capacities
          size_t item_end = item_start;
          uint64_t current_id = 0;
          size_t distinct_pools = 0;
          size_t total_contacts = 0;
          size_t total_triples = 0;
          while (item_end < eval_items.size()) {
            if (item_end == item_start || eval_items[item_end].pool->id != current_id) {
              const auto &tpool = eval_items[item_end].pool;
              size_t n_trip = tpool->gpu_triples.size();
              if (num_candidates > 0 && num_candidates < n_trip) {
                n_trip = num_candidates;
              }
              if (distinct_pools >= MAX_POOLS_PER_DISPATCH ||
                  total_triples + n_trip > MAX_TRIPLES_PER_DISPATCH) {
                if (distinct_pools > 0) break;
              }
              current_id = tpool->id;
              distinct_pools++;
              total_triples += n_trip;
              total_contacts += tpool->contacts.size();
            }
            item_end++;
          }

          size_t chunk_size = item_end - item_start;
          std::vector<GpuBox> active_gpu_boxes;
          active_gpu_boxes.reserve(chunk_size);
          std::vector<GpuResult> chunk_results(chunk_size);

          all_contacts.clear();
          all_contacts.reserve(total_contacts);
          all_triples.clear();
          all_triples.reserve(total_triples);

          // Pass 2: Populate contacts, triples, and boxes directly without hash map.
          // Because eval_items is sorted by pool->id, all identical pools are contiguous.
          MaybeMiniStatus("items");

          uint64_t last_pool_id = 0;
          int cur_contact_offset = 0;
          int cur_triple_offset = 0;
          int cur_num_triples = 0;
          int cur_num_contacts = 0;

          for (size_t i = item_start; i < item_end; i++) {
            const auto &node = current_batch[eval_items[i].batch_idx];
            const auto &tpool = eval_items[i].pool;

            if (i == item_start || tpool->id != last_pool_id) {
              last_pool_id = tpool->id;
              cur_contact_offset = (int)all_contacts.size();
              cur_triple_offset = (int)all_triples.size();
              cur_num_contacts = (int)tpool->contacts.size();

              int n_trip = (int)tpool->gpu_triples.size();
              if (num_candidates > 0 && num_candidates < (size_t)n_trip) {
                n_trip = (int)num_candidates;
              }
              cur_num_triples = n_trip;

              all_contacts.insert(all_contacts.end(),
                                  tpool->contacts.begin(), tpool->contacts.end());
              all_triples.insert(all_triples.end(),
                                 tpool->gpu_triples.begin(),
                                 tpool->gpu_triples.begin() + n_trip);
            }

            GpuBox box;
            box.cx = node.box.center.x;
            box.cy = node.box.center.y;
            box.cz = node.box.center.z;
            box.rx = node.box.radii.x;
            box.ry = node.box.radii.y;
            box.rz = node.box.radii.z;
            for (int c = 0; c < 3; c++) {
              box.tri[c][0] = node.tri.corners[c].x;
              box.tri[c][1] = node.tri.corners[c].y;
              box.tri[c][2] = node.tri.corners[c].z;
            }
            box.chart = node.chart;
            box.triple_offset = cur_triple_offset;
            box.num_triples = cur_num_triples;
            box.contact_offset = cur_contact_offset;
            box.num_contacts = cur_num_contacts;
            box._pad = 0;

            active_gpu_boxes.push_back(box);
          }


          if (use_gpu && cl != nullptr && !active_gpu_boxes.empty() &&
              !all_triples.empty()) {
            MaybeMiniStatus("gpu");

            cl_int err = CL_SUCCESS;
            cl_mem b_boxes = clCreateBuffer(
                cl->context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                sizeof(GpuBox) * active_gpu_boxes.size(), active_gpu_boxes.data(),
                &err);
            CHECK_EQ(err, CL_SUCCESS) << "clCreateBuffer b_boxes failed: " << err;
            cl_mem b_contacts = clCreateBuffer(
                cl->context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                sizeof(GpuContact) * all_contacts.size(), all_contacts.data(),
                &err);
            CHECK_EQ(err, CL_SUCCESS)
                << "clCreateBuffer b_contacts failed: " << err;
            cl_mem b_triples = clCreateBuffer(
                cl->context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                sizeof(GpuTriple) * all_triples.size(), all_triples.data(), &err);
            CHECK_EQ(err, CL_SUCCESS)
                << "clCreateBuffer b_triples failed ("
                << (sizeof(GpuTriple) * all_triples.size() / (1024 * 1024))
                << " MB): " << err;
            cl_mem b_results = clCreateBuffer(
                cl->context, CL_MEM_WRITE_ONLY,
                sizeof(GpuResult) * active_gpu_boxes.size(), nullptr, &err);
            CHECK_EQ(err, CL_SUCCESS)
                << "clCreateBuffer b_results failed: " << err;

            int num_boxes = active_gpu_boxes.size();
            err = clSetKernelArg(kernel, 0, sizeof(cl_mem), &b_boxes);
            CHECK_EQ(err, CL_SUCCESS);
            err = clSetKernelArg(kernel, 1, sizeof(cl_mem), &b_contacts);
            CHECK_EQ(err, CL_SUCCESS);
            err = clSetKernelArg(kernel, 2, sizeof(cl_mem), &b_triples);
            CHECK_EQ(err, CL_SUCCESS);
            err = clSetKernelArg(kernel, 3, sizeof(cl_mem), &b_results);
            CHECK_EQ(err, CL_SUCCESS);
            err = clSetKernelArg(kernel, 4, sizeof(int), &num_boxes);
            CHECK_EQ(err, CL_SUCCESS);

            size_t global_work_size = ((num_boxes + 63) / 64) * 64;
            size_t local_work_size = 64;
            err = clEnqueueNDRangeKernel(cl->queue, kernel, 1, nullptr,
                                         &global_work_size, &local_work_size, 0,
                                         nullptr, nullptr);
            CHECK_EQ(err, CL_SUCCESS) << "clEnqueueNDRangeKernel failed: " << err;
            err = clEnqueueReadBuffer(cl->queue, b_results, CL_TRUE, 0,
                                      sizeof(GpuResult) * num_boxes,
                                      chunk_results.data(), 0, nullptr, nullptr);
            CHECK_EQ(err, CL_SUCCESS) << "clEnqueueReadBuffer failed: " << err;

            clReleaseMemObject(b_boxes);
            clReleaseMemObject(b_contacts);
            clReleaseMemObject(b_triples);
            clReleaseMemObject(b_results);

          } else {
            // Multi-threaded CPU fallback
            MaybeMiniStatus("cpu");
            ParallelComp(active_gpu_boxes.size(), [&](int64_t a) {
              chunk_results[a] = EvaluateBoxCPU(active_gpu_boxes[a],
                                                 all_contacts, all_triples);
            }, num_threads);
          }

          for (size_t i = 0; i < chunk_size; i++) {
            results[eval_items[item_start + i].batch_idx] = chunk_results[i];
          }

          item_start = item_end;
        }

        MaybeMiniStatus("subdiv");
        for (int idx : eval_indices) {
          evaluated_count++;
          const auto &node = current_batch[idx];
          auto res = results[idx];

          bool near_depth_out = (node.depth >= max_depth - 2);
          bool deep_box_with_narrow_view =
              (node.box_depth >= max_box_depth && node.view_depth >= 20);
          bool at_limit = (node.depth >= max_depth ||
                           (node.box_depth >= max_box_depth &&
                            node.view_depth >= max_view_depth));

          if (!res.certified && (at_limit || near_depth_out || deep_box_with_narrow_view)) {
            // Stubborn leaf has reached max box depth with refined view, or tree depth limit.
            // Safety-net escalation before declaring difficult / shelving:
            for (int cs : {14, 16}) {
              MaybeMiniStatus("cpu");
              if (EffectiveConeSamples(node) >= cs) continue;
              auto esc_pool = GetTrianglePool(node.tri, cs);
              GpuBox gb;
              gb.cx = node.box.center.x;
              gb.cy = node.box.center.y;
              gb.cz = node.box.center.z;
              gb.rx = node.box.radii.x;
              gb.ry = node.box.radii.y;
              gb.rz = node.box.radii.z;
              for (int c = 0; c < 3; c++) {
                gb.tri[c][0] = node.tri.corners[c].x;
                gb.tri[c][1] = node.tri.corners[c].y;
                gb.tri[c][2] = node.tri.corners[c].z;
              }
              gb.chart = node.chart;
              gb.triple_offset = 0;
              gb.num_triples = esc_pool->gpu_triples.size();
              gb.contact_offset = 0;
              gb.num_contacts = (int)esc_pool->contacts.size();
              gb._pad = 0;
              GpuResult esc_res =
                EvaluateBoxCPU(gb, esc_pool->contacts, esc_pool->gpu_triples);
              if (esc_res.certified) {
                ctr_esc_certified++;
                res = esc_res;
                break;
              }
            }
          }

          if (res.certified) {
            RecordCertification(node);
            OutputRow(
                std::format("CE {} {} {} {} {:.17g} {} {} {}\n",
                            node.id, node.parent_id, node.depth,
                            res.winning_triple, res.margin,
                            res.inner[0], res.inner[1], res.inner[2]));
            if (prioritize_related && !active_priority.empty()) {
              for (auto it = active_priority.begin(); it != active_priority.end(); ) {
                if (it->chart == node.chart && node.box.Contains(it->w) &&
                    node.tri.ContainsRay(it->view)) {
                  if (!it->label.empty()) {
                    status.Print(AGREEN("✔") " "
                                 "{} excluded at depth {} (margin = {:.17g})!\n",
                                 it->label, node.depth, res.margin);
                    status.Print("  Node {}: box_depth={}, view_depth={}, box.c=({:.10g}, {:.10g}, {:.10g}), r=({:.10g}, {:.10g}, {:.10g})\n"
                                 "  tri=[({:.10g}, {:.10g}, {:.10g}), ({:.10g}, {:.10g}, {:.10g}), ({:.10g}, {:.10g}, {:.10g})]\n"
                                 "  winning_triple={}, inner=[{}, {}, {}]\n",
                                 node.id, node.box_depth, node.view_depth,
                                 node.box.center.x, node.box.center.y, node.box.center.z,
                                 node.box.radii.x, node.box.radii.y, node.box.radii.z,
                                 node.tri.corners[0].x, node.tri.corners[0].y, node.tri.corners[0].z,
                                 node.tri.corners[1].x, node.tri.corners[1].y, node.tri.corners[1].z,
                                 node.tri.corners[2].x, node.tri.corners[2].y, node.tri.corners[2].z,
                                 res.winning_triple, res.inner[0], res.inner[1], res.inner[2]);
                  } else {
                    status.Print(AGREEN("✔") " "
                                 "Related sol #{} excluded at "
                                 "depth {} (margin = {:.17g})!\n",
                                 it->solution_id, node.depth, res.margin);
                  }
                  it = active_priority.erase(it);
                } else {
                  ++it;
                }
              }
            }

          } else {
            if (node.depth >= max_depth ||
                (node.box_depth >= max_box_depth &&
                 node.view_depth >= max_view_depth)) {
              if (auto witness = CheckSolutionWitness(node)) {
                status.Print(
                    "\n"
                    "==============================================\n"
                    ARED("*** SOLVED: RUPERT PASSAGE CONFIRMED! ***") "\n"
                    "==============================================\n"
                    "Clearance: {:.17g}\n"
                    "Outer Frame:\n{}\n"
                    "Inner Frame:\n{}\n"
                    "Aborting proof search.\n",
                    witness->clearance,
                    SolutionDB::FrameString(witness->outer_frame),
                    SolutionDB::FrameString(witness->inner_frame));
                SolutionDB db;
                db.AddSolution("nopert_229", witness->outer_frame,
                               witness->inner_frame,
                               SolutionDB::METHOD_TILT_GRAD, 0, 0.9998,
                               witness->clearance);
                status.Print(
                    "Saved confirmed solution to " AGREEN("ruperts.sqlite")
                    ".\n");
                exit(0);
              }

              difficult_count++;
              if (difficult_file) {
                Print(difficult_file,
                      "{} {} {} {} {} {} {:.17g} {:.17g} {:.17g} {:.17g} {:.17g} {:.17g} "
                      "{:.17g} {:.17g} {:.17g} {:.17g} {:.17g} {:.17g} {:.17g} {:.17g} {:.17g} {:.17g}\n",
                      node.id, node.parent_id,
                      (int)node.depth, (int)node.box_depth, (int)node.view_depth, (int)node.chart,
                      node.box.center.x, node.box.center.y, node.box.center.z,
                      node.box.radii.x, node.box.radii.y, node.box.radii.z,
                      node.tri.corners[0].x, node.tri.corners[0].y, node.tri.corners[0].z,
                      node.tri.corners[1].x, node.tri.corners[1].y, node.tri.corners[1].z,
                      node.tri.corners[2].x, node.tri.corners[2].y, node.tri.corners[2].z,
                      res.margin);
                std::fflush(difficult_file);
              }
              OutputRow(std::format("DF {} {} {} {:.17g}\n",
                                    node.id, node.parent_id, node.depth, res.margin));

              depth_out_per.RunIf([&]{
                status.Print(
                    ARED("Shelved difficult cell #{} ")
                    "(depth={}, box_depth={}, view_depth={}, "
                    "radii=({:.6g}, {:.6g}, {:.6g})) "
                    "at box ({:.6g}, {:.6g}, {:.6g}) "
                    "(margin: {:.6g}, total difficult: {})\n",
                    node.id, node.depth, node.box_depth, node.view_depth,
                    node.box.radii.x, node.box.radii.y, node.box.radii.z,
                    node.box.center.x, node.box.center.y, node.box.center.z,
                    res.margin, FormatNum(difficult_count.Read()));
              });
              continue;
            }

            if (node.depth >= suspicious_depth) {
              suspicious_per.RunIf([&]{
                vec3 vc = node.tri.Centroid();
                status.Print(
                    AYELLOW("[{}] Hard point at suspicious depth (depth={}, box_depth={}, view_depth={}):\n")
                    "  w = ({:.17g}, {:.17g}, {:.17g})\n"
                    "  view = ({:.17g}, {:.17g}, {:.17g})\n"
                    "  radii = ({:.10g}, {:.10g}, {:.10g})\n",
                    ANSI::Time(timer.Seconds()),
                    node.depth, node.box_depth, node.view_depth,
                    node.box.center.x, node.box.center.y, node.box.center.z,
                    vc.x, vc.y, vc.z,
                    node.box.radii.x, node.box.radii.y, node.box.radii.z);
              });
            }

            int widest = node.box.WidestAxis();
            // Balanced subdivision: refine box first down to 1/512, then balance
            // using angular equipartition with a strict 2:1 rate-limiting constraint
            // (at least 2 box splits between view splits) to converge to equiangular
            // without exploding unique view triangles.
            bool split_box;
            if (node.box_depth >= max_box_depth) {
              split_box = false;
            } else if (node.view_depth >= max_view_depth) {
              split_box = true;
            } else if (node.box.radii[widest] > 1.0 / 512.0) {
              // Spatial partitioning phase: refine Cayley box down to 1/512
              // early so coarse volume pruning can reject large regions rapidly.
              split_box = true;
            } else {
              double rot_diam = 2.0 * node.box.radii[widest];
              double view_diam = node.tri.AngularDiameter();
              if (rot_diam >= split_kappa * view_diam) {
                // Box has greater angular uncertainty: split box!
                split_box = true;
              } else {
                // View has greater angular uncertainty: wants view split,
                // but rate-limit to at least 2 box splits between view splits (2:1 pacing).
                split_box = (node.box_splits_since_view < 2);
              }
            }

            if (split_box) {
              auto [b0, b1] = node.box.Split(widest);

              SearchNode child0 = node;
              child0.id = next_node_id++;
              child0.parent_id = node.id;
              child0.depth = node.depth + 1;
              child0.box_depth = node.box_depth + 1;
              child0.box_splits_since_view = node.box_splits_since_view + 1;
              child0.box = b0;

              SearchNode child1 = node;
              child1.id = next_node_id++;
              child1.parent_id = node.id;
              child1.depth = node.depth + 1;
              child1.box_depth = node.box_depth + 1;
              child1.box_splits_since_view = node.box_splits_since_view + 1;
              child1.box = b1;

              split_count++;
              OutputRow(std::format("SP {} {} {} {} {}\n",
                                    node.id, node.parent_id, node.depth,
                                    child0.id, child1.id));

              if (ContainsUncertifiedPriority(child0)) {
                stack.push_back(child1);
                stack.push_back(child0);
              } else if (ContainsUncertifiedPriority(child1)) {
                stack.push_back(child0);
                stack.push_back(child1);
              } else {
                stack.push_back(child1);
                stack.push_back(child0);
              }
            } else {
              split_count++;
              auto sub_tris = node.tri.Subdivide();
              int priority_idx = -1;
              for (int t = 0; t < 4; t++) {
                SearchNode child = node;
                child.tri = sub_tris[t];
                if (ContainsUncertifiedPriority(child)) {
                  priority_idx = t;
                  break;
                }
              }

              int64_t child_ids[4] = {-1, -1, -1, -1};
              for (int t = sub_tris.size() - 1; t >= 0; t--) {
                if (t == priority_idx) continue;
                SearchNode child = node;
                child.id = next_node_id++;
                child.parent_id = node.id;
                child.depth = node.depth + 1;
                child.view_depth = node.view_depth + 1;
                child.box_splits_since_view = 0;
                child.tri = sub_tris[t];
                child_ids[t] = child.id;
                stack.push_back(child);
              }
              if (priority_idx >= 0) {
                SearchNode child = node;
                child.id = next_node_id++;
                child.parent_id = node.id;
                child.depth = node.depth + 1;
                child.view_depth = node.view_depth + 1;
                child.box_splits_since_view = 0;
                child.tri = sub_tris[priority_idx];
                child_ids[priority_idx] = child.id;
                stack.push_back(child);
              }

              OutputRow(std::format("SV {} {} {} {} {} {} {}\n",
                                    node.id, node.parent_id, node.depth,
                                    child_ids[0], child_ids[1], child_ids[2], child_ids[3]));
            }
          }
        }
      }

      if (progress_per.ShouldRun()) {
        double elapsed = timer.Seconds();
        double rate = evaluated_count.Read() / std::max(1e-6, elapsed);
        double completed_frac = CompletedFraction();

        std::string pool_str;
        if (prioritize_related) {
          pool_str = std::format("{}/{} priority left",
                                 active_priority.size(),
                                 num_priority_points);
        } else {
          pool_str = AGREY("Related solution pool: inactive");
        }

        std::string row0 = StatusCounters();

        std::string depth_hist_str = FormatDepthHistogram();
        status.Status(
            "{}\n"
            "Stack: {} " AGREY("|")
            " Done: {:.4f}% " AGREY("|")
            " Depth: {} " AGREY("|")
            " {} boxes/s " AGREY("|")
            " {}\n"
            "{}\n"
            "{}",
            row0,
            FormatNum(stack.size()),
            completed_frac * 100.0,
            current_batch.empty() ? 0 : current_batch[0].depth,
            FormatNum((int64_t)rate), ANSI::Time(elapsed),
            depth_hist_str,
            pool_str);
        // Only output incremental status updates if the major status output
        // hasn't run recently.
        mini_status_per.Reset();
        last_op.clear();
      }

      if (checkpoint_per.ShouldRun() || sigint_received.load()) {
        SaveCheckpoint(ckpt_path);
        FlushRows();
        if (difficult_file) std::fflush(difficult_file);
        if (sigint_received.load()) {
          PrintDepthDistribution();
          status.Print("\n"
                       AYELLOW("Interrupted (SIGINT)") ".\n"
                       "Saved checkpoint with {} nodes to {}.\n"
                       "Exiting...\n",
                       FormatNum(stack.size()), ckpt_path);
          std::fflush(stdout);
          std::fflush(stderr);
          break;
        }
      }
    }


    if (!sigint_received.load() && stack.empty()) {
      // Completed full tree!
      std::error_code ec;
      std::filesystem::remove(ckpt_path, ec);
      status.Print(AGREEN("\n=== ☻ Search Completed ☻ ===\n")
                   "Took {}s.",
                   ANSI::Time(timer.Seconds()));
      PrintDepthDistribution();
    }

    FlushRows();
    fclose(row_file);
    if (difficult_file) {
      std::fflush(difficult_file);
      fclose(difficult_file);
      difficult_file = nullptr;
    }
    status.Print("Total evaluated: {}\n"
                 "Total certified: {}\n"
                 "Total difficult (shelved): {}\n"
                 "Total pruned: {}\n"
                 "Total splits: {}\n",
                  evaluated_count.Read(),
                  certified_count.Read(),
                  difficult_count.Read(),
                  pruned_count.Read(),
                  split_count.Read());
  }

  static std::string StatusCounters() {
    #define BAR " " ANSI_GREY "|" ANSI_DARK_WHITE " "
    return std::format(
        ANSI_BG(0, 0, 80)
        "Loop: {}" BAR
        "Eval: {}" BAR
        "Cert: {}" BAR
        "Diff: {}" BAR
        "Pruned: {}" BAR
        "{}⊿" BAR
        "{}/{}⚡ "
        ANSI_RESET,
        FormatNum(ctr_loops.Read()),
        FormatNum(evaluated_count.Read()),
        FormatNum(certified_count.Read()),
        FormatNum(difficult_count.Read()),
        FormatNum(pruned_count.Read()),
        FormatNum(ctr_built_triangles.Read()),
        FormatNum(ctr_esc_certified.Read()),
        FormatNum(ctr_cpu.Read()));
  }

  void MaybeMiniStatus(std::string_view op) {
    bool op_changed = (op != last_op);
    if (op_changed || mini_status_per.ShouldRun()) {
      if (op_changed) {
        mini_status_per.Reset();
        last_op = op;
      }
      status.LineStatus(0, "{} " ABLUE("{}"), StatusCounters(), op);
    }
  }

  void TestKnownSolution() {
    Print(AYELLOW("Testing Rupert solution detection on "
                  "known solution 1662...\n"));

    auto o_frame = SolutionDB::StringFrame(
        "-0.49746192508304149,0.72491410864775851,-0.47647787795038249,"
        "0.077349272600657409,0.58414142401727587,0.80795784962782424,"
        "0.86403051052658031,0.36507304999204515,-0.34665969631424132,0,0,0");
    auto i_frame = SolutionDB::StringFrame(
        "-0.49713703400231984,0.72470320344367012,-0.4771373348857319,"
        "0.076797862455929219,0.58449836059071414,0.80775228553620826,"
        "0.86426665893436894,0.36492044802292239,-0.34623143830272424,"
        "6.2363539751420424e-05,-4.037091880670955e-05,0");

    CHECK(o_frame && i_frame) << "Failed to parse frames!";

    Polyhedron poly = GetPolyhedron229();
    auto initial_c = GetClearance(poly, *o_frame, *i_frame);
    Print("Direct GetClearance on frames: {}\n",
          initial_c ? std::format("SUCCESS ({:.17g})", *initial_c) : "FAILED");

    // View direction is outer_frame column 2 in world coordinates mapped to
    // poly space:
    vec3 sol_view = {o_frame->x.z, o_frame->y.z, o_frame->z.z};
    sol_view = yocto::normalize(sol_view);

    // Relative rotation R = R_outer^T * R_inner
    double R[3][3];
    vec3 out_cols[3] = {o_frame->x, o_frame->y, o_frame->z};
    vec3 inn_cols[3] = {i_frame->x, i_frame->y, i_frame->z};
    for (int i = 0; i < 3; i++) {
      for (int j = 0; j < 3; j++) {
        R[i][j] = yocto::dot(out_cols[i], inn_cols[j]);
      }
    }
    double tr = R[0][0] + R[1][1] + R[2][2];
    double denom = 1.0 + tr;
    vec3 sol_w = {
      (R[2][1] - R[1][2]) / denom,
      (R[0][2] - R[2][0]) / denom,
      (R[1][0] - R[0][1]) / denom
    };

    Print("Extracted Cayley w: ({:.17g}, {:.17g}, {:.17g})\n",
          sol_w.x, sol_w.y, sol_w.z);
    Print("Extracted View v:  ({:.17g}, {:.17g}, {:.17g})\n",
          sol_view.x, sol_view.y, sol_view.z);

    // Test 1: Exact box centered at solution
    SearchNode node1;
    node1.id = 1;
    node1.depth = 64;
    node1.chart = 0;
    node1.box.center = sol_w;
    node1.box.radii = {1e-6, 1e-6, 1e-6};

    vec3 p0 = sol_view + vec3{1e-5, 0.0, 0.0};
    vec3 p1 = sol_view + vec3{-0.5e-5, 0.866e-5, 0.0};
    vec3 p2 = sol_view + vec3{-0.5e-5, -0.866e-5, 0.0};
    node1.tri.corners[0] = yocto::normalize(p0);
    node1.tri.corners[1] = yocto::normalize(p1);
    node1.tri.corners[2] = yocto::normalize(p2);

    Print("\n--- Case 1: Box centered at solution ---\n");
    auto witness1 = CheckSolutionWitness(node1);
    if (witness1) {
      Print(AGREEN("Case 1 PASSED: Verified clearance {:.17g}\n"),
            witness1->clearance);
    } else {
      Print(ARED("Case 1 FAILED!\n"));
    }

    // Test 2: Offset box where solution is in the interior, NOT at the center
    SearchNode node2 = node1;
    node2.id = 2;
    node2.box.center = sol_w + vec3{2.5e-7, -3.0e-7, 1.8e-7};
    node2.box.radii = {1e-6, 1e-6, 1e-6};

    Print("\n--- Case 2: Offset box (solution is interior, not at center) ---\n");
    auto witness2 = CheckSolutionWitness(node2);
    if (witness2) {
      Print(AGREEN("Case 2 PASSED: Verified clearance {:.17g}\n"
                   "Outer Frame:\n{}\n"
                   "Inner Frame:\n{}\n"),
            witness2->clearance,
            SolutionDB::FrameString(witness2->outer_frame),
            SolutionDB::FrameString(witness2->inner_frame));
    } else {
      Print(ARED("Case 2 FAILED!\n"));
    }

    Print("\n--- Part 3: Live tree-search subdivision test from depth 60 to max_depth 64 ---\n");
    SearchNode ancestor;
    ancestor.id = next_node_id++;
    ancestor.parent_id = -1;
    ancestor.depth = 60;
    ancestor.box_depth = 48;
    ancestor.view_depth = 12;
    ancestor.chart = 0;
    ancestor.box.center = sol_w;
    ancestor.box.radii = {1e-5, 1e-5, 1e-5};
    ancestor.tri = node1.tri;

    this->stack.clear();
    this->stack.push_back(ancestor);
    this->max_depth = 64;
    this->batch_size = 64;
    this->resume = false;
    this->output_dir = ".artifacts/test_solution";
    this->Run();
  }

  void TestValleyPoint() {
    Print(AYELLOW("Testing valley hard points directly...\n"));
    for (int test_idx = 0; test_idx < 3; test_idx++) {
      vec3 sol_w = (test_idx == 0)
          ? vec3{0.00030419230461120605, -0.00036638975143432617, -0.0005896488825480144}
          : (test_idx == 1)
          ? vec3{0.0003413856029510498, -0.00041025876998901367, -0.00066292285919189442}
          : vec3{0.00041022896766662598, -0.00049299001693725586, -0.0007966756820678712};
      vec3 sol_view = yocto::normalize(vec3{0.9164627443138856, 0.2030404322634455, 0.3443121541818299});

      Print("Testing point {}: w=({:.8g}, {:.8g}, {:.8g})\n", test_idx, sol_w.x, sol_w.y, sol_w.z);

      SearchNode ancestor;
      ancestor.id = next_node_id++;
      ancestor.parent_id = -1;
      ancestor.depth = 80;
      ancestor.box_depth = 64;
      ancestor.view_depth = 16;
      ancestor.chart = 0;
      ancestor.box.center = sol_w;
      ancestor.box.radii = {1e-6, 1e-6, 1e-6};

      vec3 p0 = sol_view + vec3{1e-5, 0.0, 0.0};
      vec3 p1 = sol_view + vec3{-0.5e-5, 0.866e-5, 0.0};
      vec3 p2 = sol_view + vec3{-0.5e-5, -0.866e-5, 0.0};
      ancestor.tri.corners[0] = yocto::normalize(p0);
      ancestor.tri.corners[1] = yocto::normalize(p1);
      ancestor.tri.corners[2] = yocto::normalize(p2);

      this->stack.clear();
      this->stack.push_back(ancestor);
      this->max_depth = 96;
      this->max_box_depth = 72;
      this->max_view_depth = 24;
      this->batch_size = 64;
      this->resume = false;
      this->output_dir = ".artifacts/test_valley";
      this->Run();
    }
  }

  void TestDepthOut() {
    Print(AYELLOW("Testing depth-out leaf directly with EvaluateBoxCPU...\n"));
    vec3 sol_w = {0.0003413856029510498, -0.00041025876998901367, -0.00066292285919189442};
    vec3 sol_r = {2.9802322387695312e-08, 5.9604644775390625e-08, 3.9736429850260414e-08};

    SearchNode node;
    node.id = 0;
    node.parent_id = -1;
    node.depth = 96;
    node.box_depth = 72;
    node.view_depth = 24;
    node.chart = 0;
    node.box.center = sol_w;
    node.box.radii = sol_r;
    node.tri.corners[0] = {0.6255792583, 0.1381680762, 0.2362526655};
    node.tri.corners[1] = {0.6255792881, 0.1381680762, 0.2362526357};
    node.tri.corners[2] = {0.6255792656, 0.1381680987, 0.2362526357};

    auto tpool = GetTrianglePool(node.tri, 14);
    GpuBox gbox;
    gbox.cx = node.box.center.x; gbox.cy = node.box.center.y; gbox.cz = node.box.center.z;
    gbox.rx = node.box.radii.x;  gbox.ry = node.box.radii.y;  gbox.rz = node.box.radii.z;
    for (int c = 0; c < 3; c++) {
      gbox.tri[c][0] = node.tri.corners[c].x;
      gbox.tri[c][1] = node.tri.corners[c].y;
      gbox.tri[c][2] = node.tri.corners[c].z;
    }
    gbox.chart = node.chart;
    gbox.triple_offset = 0;
    gbox.num_triples = tpool->gpu_triples.size();
    gbox.contact_offset = 0;
    gbox.num_contacts = tpool->contacts.size();
    gbox._pad = 0;
    auto res = EvaluateBoxCPU(gbox, tpool->contacts, tpool->gpu_triples);
    Print("At depth 96 (box_depth 72, view_depth 24): certified={}, winning_triple={}, margin={:.17g}\n",
          res.certified, res.winning_triple, res.margin);

    // Now test both halves of the bisected box (box_depth 73)
    GpuBox gbox0 = gbox;
    gbox0.cy = node.box.center.y - node.box.radii.y * 0.5;
    gbox0.ry = node.box.radii.y * 0.5;
    auto res0 = EvaluateBoxCPU(gbox0, tpool->contacts, tpool->gpu_triples);

    GpuBox gbox1 = gbox;
    gbox1.cy = node.box.center.y + node.box.radii.y * 0.5;
    gbox1.ry = node.box.radii.y * 0.5;
    auto res1 = EvaluateBoxCPU(gbox1, tpool->contacts, tpool->gpu_triples);

    Print("Child 0 (box_depth 73): certified={}, winning_triple={}, margin={:.17g}\n",
          res0.certified, res0.winning_triple, res0.margin);
    Print("Child 1 (box_depth 73): certified={}, winning_triple={}, margin={:.17g}\n",
          res1.certified, res1.winning_triple, res1.margin);

    // Bisect Child 1's widest remaining axis (rx or rz)
    // At box_depth 73, rx = 2.98e-8, ry = 2.98e-8, rz = 3.97e-8 -> widest is rz!
    GpuBox gbox10 = gbox1;
    gbox10.cz = gbox1.cz - gbox1.rz * 0.5;
    gbox10.rz = gbox1.rz * 0.5;
    auto res10 = EvaluateBoxCPU(gbox10, tpool->contacts, tpool->gpu_triples);

    GpuBox gbox11 = gbox1;
    gbox11.cz = gbox1.cz + gbox1.rz * 0.5;
    gbox11.rz = gbox1.rz * 0.5;
    auto res11 = EvaluateBoxCPU(gbox11, tpool->contacts, tpool->gpu_triples);

    Print("Child 1.0 (box_depth 74): certified={}, winning_triple={}, margin={:.17g}\n",
          res10.certified, res10.winning_triple, res10.margin);
    Print("Child 1.1 (box_depth 74): certified={}, winning_triple={}, margin={:.17g}\n",
          res11.certified, res11.winning_triple, res11.margin);

    Print(AYELLOW("\n--- Testing shelving difficult leaf to chart0.difficult ---\n"));
    SearchNode diff_node = node;
    diff_node.id = next_node_id++;
    diff_node.depth = 96;
    diff_node.box_depth = 72;
    diff_node.view_depth = 24;
    this->stack.clear();
    this->stack.push_back(diff_node);
    this->max_depth = 96;
    this->max_box_depth = 72;
    this->max_view_depth = 24;
    this->batch_size = 64;
    this->resume = false;
    this->output_dir = ".artifacts/test_difficult";
    this->Run();
  }
};

int main(int argc, char **argv) {
  ANSI::Init();
  std::signal(SIGINT, SigHandler);
  std::signal(SIGTERM, SigHandler);

  SearchManager mgr;

  for (int i = 1; i < argc; i++) {
    std::string_view arg = argv[i];
    if (arg == "--chart" && i + 1 < argc) {
      mgr.chart = std::atoi(argv[++i]);
    } else if (arg == "--batch_size" && i + 1 < argc) {
      mgr.batch_size = std::atoi(argv[++i]);
    } else if (arg == "--max_depth" && i + 1 < argc) {
      mgr.max_depth = std::atoi(argv[++i]);
    } else if (arg == "--max_box_depth" && i + 1 < argc) {
      mgr.max_box_depth = std::atoi(argv[++i]);
    } else if (arg == "--max_view_depth" && i + 1 < argc) {
      mgr.max_view_depth = std::atoi(argv[++i]);
    } else if (arg == "--suspicious_depth" && i + 1 < argc) {
      mgr.suspicious_depth = std::atoi(argv[++i]);
    } else if (arg == "--candidates" && i + 1 < argc) {
      mgr.num_candidates = std::atoi(argv[++i]);
    } else if (arg == "--cone_samples" && i + 1 < argc) {
      mgr.cone_samples = std::atoi(argv[++i]);
    } else if (arg == "--escalate_depth" && i + 1 < argc) {
      mgr.escalate_depth = std::atoi(argv[++i]);
    } else if (arg == "--escalate_cone_samples" && i + 1 < argc) {
      mgr.escalate_cone_samples = std::atoi(argv[++i]);
    } else if (arg == "--deep_escalate_depth" && i + 1 < argc) {
      mgr.deep_escalate_depth = std::atoi(argv[++i]);
    } else if (arg == "--deep_escalate_cone_samples" && i + 1 < argc) {
      mgr.deep_escalate_cone_samples = std::atoi(argv[++i]);
    } else if (arg == "--threads" && i + 1 < argc) {
      mgr.num_threads = std::atoi(argv[++i]);
    } else if (arg == "--output_dir" && i + 1 < argc) {
      mgr.output_dir = argv[++i];
    } else if (arg == "--tube_radius" && i + 1 < argc) {
      mgr.tube_radius = std::atof(argv[++i]);
    } else if (arg == "--split_kappa" && i + 1 < argc) {
      mgr.split_kappa = std::atof(argv[++i]);
    } else if (arg == "--triangle_cache" && i + 1 < argc) {
      g_max_triangle_cache_size = std::atoll(argv[++i]);
    } else if (arg == "--cpu") {
      mgr.use_gpu = false;
    } else if (arg == "--gpu") {
      mgr.use_gpu = true;
    } else if (arg == "--fresh") {
      mgr.resume = false;
    } else if (arg == "--resume") {
      mgr.resume = true;
    } else if (arg == "--no_resume") {
      mgr.resume = false;
    } else if (arg == "--prioritize_related") {
      mgr.prioritize_related = true;
    } else if (arg == "--no_prioritize_related") {
      mgr.prioritize_related = false;
    } else if (arg == "--related_epsilon" && i + 1 < argc) {
      mgr.related_epsilon = std::atof(argv[++i]);
    } else if (arg == "--test_solution") {
      mgr.TestKnownSolution();
      return 0;
    } else if (arg == "--test_valley") {
      mgr.TestValleyPoint();
      return 0;
    } else if (arg == "--test_depth_out") {
      mgr.TestDepthOut();
      return 0;

    } else if (arg == "--help" || arg == "-h") {
      Print("Usage: ./lean229.exe [options]\n"
            "  --chart <0|1|2>     Cayley chart index (default 0)\n"
            "  --batch_size <N>    Batch size for GPU/evaluator (default 32768)\n"
            "  --max_depth <D>     Maximum branch-and-bound tree depth (default 80)\n"
            "  --max_box_depth <D> Maximum Cayley box subdivision depth (default 60)\n"
            "  --max_view_depth <D> Maximum view triangle subdivision depth (default 20)\n"
            "  --suspicious_depth <D> Threshold to report hard points (default 70)\n"
            "  --candidates <N>    Maximum candidate triples to test per box (default 0 = all)\n"
            "  --cone_samples <N>  Silhouette cone samples per vertex (default 8)\n"
            "  --escalate_depth <D> Tree depth to escalate cone samples (default 36)\n"
            "  --escalate_cone_samples <N> Escalated cone samples (default 12)\n"
            "  --deep_escalate_depth <D> Tree depth for second cone escalation (default 60)\n"
            "  --deep_escalate_cone_samples <N> Second escalated cone samples (default 14)\n"
            "  --split_kappa <K>   Box-to-view angular diameter split bias (default 1.0)\n"
            "  --triangle_cache <N> Maximum unique triangle pools to cache in RAM (default 20480)\n"
            "  --tube_radius <R>   Identity symmetry tube radius (default 1e-4)\n"
            "  --threads <T>       CPU fallback worker threads (default 8)\n"
            "  --output_dir <DIR>  Output directory for logs and checkpoints\n"
            "  --cpu               Force multi-threaded CPU execution\n"
            "  --gpu               Use OpenCL acceleration (default)\n"
            "  --resume            Resume from checkpoint if present (default)\n"
            "  --fresh             Ignore any checkpoint and start fresh\n"
            "  --test_solution     Verify detection on known Rupert solution 1662\n");
      return -1;
    } else {
      Print("Unknown arg. Try ./lean229.exe --help\n");
      return -1;
    }
  }

  mgr.InitOpenCL();
  mgr.Run();
  return 0;
}
