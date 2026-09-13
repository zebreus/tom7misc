// High-performance certificate packer for Nopert #229.
// Ingests chart{0..2}.rows.log, reconstructs the solution tree with exact
// rational arithmetic (BigRat / GMP), validates certificate conditions,
// and outputs chart{0..2}.pack compatible with Lean 4's PackedSolutionTree.

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <string_view>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <thread>
#include <mutex>
#include <cassert>
#include <memory>
#include <span>

#include "bignum/big.h"
#include "bignum/big-overloads.h"
#include "geom/hull-2d.h"
#include "base/logging.h"
#include "ansi.h"
#include "timer.h"

// Exact rational vertices of Nopert #229 from Nopert229/Vertices.lean
static const BigRat VERTICES_Q[20][3] = {
  {BigRat("17136292830659/400000000000000"), BigRat("5680663556187131/10000000000000000"), BigRat("5648167326177671/10000000000000000")},
  {BigRat("-42773513204957/250000000000000"), BigRat("9384169756351713/10000000000000000"), BigRat("4802676718449/16000000000000")},
  {BigRat("-2791996671138589/10000000000000000"), BigRat("8916783831939151/10000000000000000"), BigRat("-420605170858861/10000000000000000")},
  {BigRat("581211699562287/10000000000000000"), BigRat("602579091333187/1000000000000000"), BigRat("-382169975802723/500000000000000")},
  {BigRat("-5270246949360691/10000000000000000"), BigRat("2162861152231751/10000000000000000"), BigRat("5648167326177671/10000000000000000")},
  {BigRat("-4726792748188159/5000000000000000"), BigRat("1272666794475643/10000000000000000"), BigRat("4802676718449/16000000000000")},
  {BigRat("-1868627957476221/2000000000000000"), BigRat("12511388959529/1250000000000000"), BigRat("-420605170858861/10000000000000000")},
  {BigRat("-2775631710731053/5000000000000000"), BigRat("1207418485492689/5000000000000000"), BigRat("-382169975802723/500000000000000")},
  {BigRat("-921399766144207/2500000000000000"), BigRat("-1085985462790287/2500000000000000"), BigRat("5648167326177671/10000000000000000")},
  {BigRat("-4131696624115331/10000000000000000"), BigRat("-2149404605253097/2500000000000000"), BigRat("4802676718449/16000000000000")},
  {BigRat("-7455953197761/25000000000000"), BigRat("-8854924122951479/10000000000000000"), BigRat("-420605170858861/10000000000000000")},
  {BigRat("-4012081174529903/10000000000000000"), BigRat("-2266669793986531/5000000000000000"), BigRat("-382169975802723/500000000000000")},
  {BigRat("46756585289803/156250000000000"), BigRat("-4847564861402479/10000000000000000"), BigRat("5648167326177671/10000000000000000")},
  {BigRat("1380011310293969/2000000000000000"), BigRat("-411642950060219/625000000000000"), BigRat("4802676718449/16000000000000")},
  {BigRat("3749963394741599/5000000000000000"), BigRat("-5572735187461599/10000000000000000"), BigRat("-420605170858861/10000000000000000")},
  {BigRat("767915222494757/2500000000000000"), BigRat("-10188661950973/19531250000000"), BigRat("-382169975802723/500000000000000")},
  {BigRat("5535017234623651/10000000000000000"), BigRat("673991002072371/5000000000000000"), BigRat("5648167326177671/10000000000000000")},
  {BigRat("1679233219444017/2000000000000000"), BigRat("2263534535574267/5000000000000000"), BigRat("4802676718449/16000000000000")},
  {BigRat("1523518189628179/2000000000000000"), BigRat("2705392183398847/5000000000000000"), BigRat("-420605170858861/10000000000000000")},
  {BigRat("5910472006450693/10000000000000000"), BigRat("327326655638497/2500000000000000"), BigRat("-382169975802723/500000000000000")}
};

// Double precision vertex coordinates for candidate ranking (identical to lean229.cc)
static const double VERTICES_D[20][3] = {
  {0.0428407320766475, 0.5680663556187131, 0.5648167326177671},
  {-0.171094052819828, 0.9384169756351713, 0.3001672949030625},
  {-0.2791996671138589, 0.8916783831939151, -0.0420605170858861},
  {0.0581211699562287, 0.602579091333187, -0.764339951605446},
  {-0.5270246949360691, 0.2162861152231751, 0.5648167326177671},
  {-0.9453585496376318, 0.1272666794475643, 0.3001672949030625},
  {-0.9343139787381105, 0.0100091111676232, -0.0420605170858861},
  {-0.5551263421462106, 0.2414836970985378, -0.764339951605446},
  {-0.3685599064576828, -0.4343941851161148, 0.5648167326177671},
  {-0.4131696624115331, -0.8597618421012388, 0.3001672949030625},
  {-0.29823812791044, -0.8854924122951479, -0.0420605170858861},
  {-0.4012081174529903, -0.4533339587973062, -0.764339951605446},
  {0.2992421458547392, -0.4847564861402479, 0.5648167326177671},
  {0.6900056551469845, -0.6586287200963504, 0.3001672949030625},
  {0.7499926789483198, -0.5572735187461599, -0.0420605170858861},
  {0.3071660889979028, -0.5216594918898176, -0.764339951605446},
  {0.5535017234623651, 0.1347982004144742, 0.5648167326177671},
  {0.8396166097220085, 0.4527069071148534, 0.3001672949030625},
  {0.7617590948140895, 0.5410784366797694, -0.0420605170858861},
  {0.5910472006450693, 0.1309306622553988, -0.764339951605446}
};

struct Vec3Q {
  BigRat x, y, z;
  Vec3Q() : x(0), y(0), z(0) {}
  Vec3Q(BigRat x, BigRat y, BigRat z) : x(x), y(y), z(z) {}

  Vec3Q operator+(const Vec3Q &o) const { return {x + o.x, y + o.y, z + o.z}; }
  Vec3Q operator-(const Vec3Q &o) const { return {x - o.x, y - o.y, z - o.z}; }
  Vec3Q operator*(const BigRat &s) const { return {x * s, y * s, z * s}; }
  Vec3Q operator/(const BigRat &s) const { return {x / s, y / s, z / s}; }

  static BigRat Dot(const Vec3Q &a, const Vec3Q &b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
  }
  static Vec3Q Cross(const Vec3Q &a, const Vec3Q &b) {
    return {
      a.y * b.z - a.z * b.y,
      a.z * b.x - a.x * b.z,
      a.x * b.y - a.y * b.x
    };
  }
  bool operator==(const Vec3Q &o) const {
    return x == o.x && y == o.y && z == o.z;
  }
};

inline Vec3Q operator*(const BigRat &s, const Vec3Q &v) { return v * s; }

struct TriangleQ {
  Vec3Q corners[3];

  std::vector<TriangleQ> Subdivide() const {
    Vec3Q m01 = (corners[0] + corners[1]) / BigRat(2);
    Vec3Q m12 = (corners[1] + corners[2]) / BigRat(2);
    Vec3Q m20 = (corners[2] + corners[0]) / BigRat(2);
    return {
      {corners[0], m01, m20},
      {m01, corners[1], m12},
      {m20, m12, corners[2]},
      {m01, m12, m20}
    };
  }

  bool operator==(const TriangleQ &o) const {
    return corners[0] == o.corners[0] &&
           corners[1] == o.corners[1] &&
           corners[2] == o.corners[2];
  }
};

struct CayleyBoxQ {
  Vec3Q center;
  Vec3Q radii;

  int WidestAxis() const {
    if (radii.x >= radii.y && radii.x >= radii.z) return 0;
    if (radii.y >= radii.z) return 1;
    return 2;
  }

  bool operator==(const CayleyBoxQ &o) const {
    return center == o.center && radii == o.radii;
  }
};

struct vec3d {
  double x, y, z;
  vec3d operator+(const vec3d &o) const { return {x + o.x, y + o.y, z + o.z}; }
  vec3d operator-(const vec3d &o) const { return {x - o.x, y - o.y, z - o.z}; }
  vec3d operator*(double s) const { return {x * s, y * s, z * s}; }
  vec3d operator/(double s) const { return {x / s, y / s, z / s}; }
};
inline double dot(const vec3d &a, const vec3d &b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
inline vec3d cross(const vec3d &a, const vec3d &b) {
  return {a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x};
}
inline double length(const vec3d &v) { return std::sqrt(dot(v, v)); }
inline vec3d normalize(const vec3d &v) { double l = length(v); return l > 0 ? v / l : v; }

struct ContactInfo {
  int vertex;
  int edge_start;
  int edge_finish;
  int edge_start2;
  int edge_finish2;
  int mix; // 0..1000
  vec3d edge;
  double defect;
};

struct GpuTriple {
  int c0, c1, c2;
  vec3d w_coeff[3];
  double weighted_defect_upper;
  double min_p;
};

struct TrianglePool {
  std::vector<ContactInfo> contacts;
  std::vector<GpuTriple> gpu_triples;
};

static double ComputeWeightedDefectUpper(const vec3d tri_corners[3],
                                         const std::vector<ContactInfo> &contacts,
                                         int c0, int c1, int c2,
                                         const vec3d w_coeff[3]) {
  double total = 0.0;
  const double error = 1e-9;
  const int c_indices[3] = {c0, c1, c2};
  for (int i = 0; i < 3; i++) {
    int sel = contacts[c_indices[i]].vertex;
    vec3d edge = contacts[c_indices[i]].edge;
    double w_vals[3] = {
      dot(tri_corners[0], w_coeff[i]) + error,
      dot(tri_corners[1], w_coeff[i]) + error,
      dot(tri_corners[2], w_coeff[i]) + error
    };
    double upper = 0.0;
    for (int k = 0; k < 20; k++) {
      if (k == sel) continue;
      vec3d delta = {
        VERTICES_D[k][0] - VERTICES_D[sel][0],
        VERTICES_D[k][1] - VERTICES_D[sel][1],
        VERTICES_D[k][2] - VERTICES_D[sel][2]
      };
      vec3d s_coeff = cross(edge, delta);
      double s_vals[3] = {
        dot(tri_corners[0], s_coeff) + error,
        dot(tri_corners[1], s_coeff) + error,
        dot(tri_corners[2], s_coeff) + error
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

static std::shared_ptr<const TrianglePool> BuildTrianglePool(const vec3d tri_corners[3], int cone_samples = 8, bool sort_triples = false) {
  auto pool = std::make_shared<TrianglePool>();
  vec3d uview = normalize((tri_corners[0] + tri_corners[1] + tri_corners[2]) / 3.0);
  int min_axis = 0;
  double min_val = std::abs(uview.x);
  if (std::abs(uview.y) < min_val) {
    min_val = std::abs(uview.y);
    min_axis = 1;
  }
  if (std::abs(uview.z) < min_val) {
    min_axis = 2;
  }
  vec3d axis = {0, 0, 0};
  if (min_axis == 0) axis.x = 1.0;
  else if (min_axis == 1) axis.y = 1.0;
  else axis.z = 1.0;

  vec3d right = normalize(cross(uview, axis));
  vec3d up = cross(uview, right);

  std::vector<yocto::vec<double, 2>> projected(20);
  for (int i = 0; i < 20; i++) {
    vec3d v = {VERTICES_D[i][0], VERTICES_D[i][1], VERTICES_D[i][2]};
    projected[i] = {dot(v, right), dot(v, up)};
  }

  std::vector<int> hull = Hull2D::GrahamScan(projected);
  int H = hull.size();
  if (H < 3) return pool;

  std::vector<ContactInfo> valid_contacts;
  for (int i = 0; i < H; i++) {
    int curr = hull[i];
    int next = hull[(i + 1) % H];
    int prev = hull[(i - 1 + H) % H];

    vec3d v_curr = {VERTICES_D[curr][0], VERTICES_D[curr][1], VERTICES_D[curr][2]};
    vec3d v_next = {VERTICES_D[next][0], VERTICES_D[next][1], VERTICES_D[next][2]};
    vec3d v_prev = {VERTICES_D[prev][0], VERTICES_D[prev][1], VERTICES_D[prev][2]};

    vec3d first = v_prev - v_curr;
    vec3d second = v_curr - v_next;

    for (int s = 0; s <= cone_samples + 1; s++) {
      double lam = (double)s / (cone_samples + 1.0);
      vec3d e = first * lam + second * (1.0 - lam);

      double max_upper = 0.0;
      double min_upper = 1e30;
      for (int k = 0; k < 20; k++) {
        if (k == curr) continue;
        vec3d delta = {VERTICES_D[k][0] - VERTICES_D[curr][0],
                       VERTICES_D[k][1] - VERTICES_D[curr][1],
                       VERTICES_D[k][2] - VERTICES_D[curr][2]};
        vec3d coeff = cross(e, delta);
        if (length(coeff) < 1e-12) continue;
        double upper = std::max({dot(tri_corners[0], coeff),
                                 dot(tri_corners[1], coeff),
                                 dot(tri_corners[2], coeff)});
        if (upper > max_upper) max_upper = upper;
        if (upper < min_upper) min_upper = upper;
      }
      if (min_upper >= 0.0) continue;

      vec3d e_norm = normalize(e);
      bool duplicate = false;
      for (const auto &ex : valid_contacts) {
        vec3d ex_norm = normalize(ex.edge);
        if (dot(e_norm, ex_norm) > 1.0 - 1e-9) {
          duplicate = true;
          break;
        }
      }
      if (duplicate) continue;

      ContactInfo c;
      c.vertex = curr;
      c.edge_start = prev;
      c.edge_finish = curr;
      c.edge_start2 = curr;
      c.edge_finish2 = next;
      c.mix = std::round(1000.0 * lam);
      c.edge = e;
      c.defect = std::max(0.0, max_upper);
      valid_contacts.push_back(c);
    }
  }

  pool->contacts = std::move(valid_contacts);
  int C = pool->contacts.size();

  struct SortableTriple {
    GpuTriple gt;
    double defect_sum;
    double min_p;
  };
  std::vector<SortableTriple> sortable;

  for (int i = 0; i < C; i++) {
    for (int j = i + 1; j < C; j++) {
      for (int k = j + 1; k < C; k++) {
        vec3d e0 = pool->contacts[i].edge;
        vec3d e1 = pool->contacts[j].edge;
        vec3d e2 = pool->contacts[k].edge;

        vec3d coeff0 = cross(e1, e2);
        vec3d coeff1 = cross(e2, e0);
        vec3d coeff2 = cross(e0, e1);

        double p0 = dot(uview, coeff0);
        double p1 = dot(uview, coeff1);
        double p2 = dot(uview, coeff2);

        int ci = i, cj = j, ck = k;
        if (p0 < 0.0 && p1 < 0.0 && p2 < 0.0) {
          std::swap(cj, ck);
          std::swap(e1, e2);
          coeff0 = cross(e1, e2);
          coeff1 = cross(e2, e0);
          coeff2 = cross(e0, e1);
          p0 = dot(uview, coeff0);
          p1 = dot(uview, coeff1);
          p2 = dot(uview, coeff2);
        }

        if (p0 <= 1e-9 || p1 <= 1e-9 || p2 <= 1e-9) continue;

        double w0_min = std::min({dot(tri_corners[0], coeff0), dot(tri_corners[1], coeff0), dot(tri_corners[2], coeff0)});
        double w1_min = std::min({dot(tri_corners[0], coeff1), dot(tri_corners[1], coeff1), dot(tri_corners[2], coeff1)});
        double w2_min = std::min({dot(tri_corners[0], coeff2), dot(tri_corners[1], coeff2), dot(tri_corners[2], coeff2)});
        // Ensure view cone corners are strictly interior (must match lean229.cc).
        if (w0_min <= 1e-11 || w1_min <= 1e-11 || w2_min <= 1e-11) continue;

        const vec3d w_coeff[3] = {coeff0, coeff1, coeff2};
        double total_defect = ComputeWeightedDefectUpper(tri_corners, pool->contacts, ci, cj, ck, w_coeff);

        if (!sort_triples) {
          GpuTriple gt;
          gt.c0 = ci; gt.c1 = cj; gt.c2 = ck;
          gt.w_coeff[0] = coeff0;
          gt.w_coeff[1] = coeff1;
          gt.w_coeff[2] = coeff2;
          gt.weighted_defect_upper = total_defect;
          gt.min_p = std::min({p0, p1, p2});
          pool->gpu_triples.push_back(gt);
        } else {
          SortableTriple st;
          st.gt.c0 = ci; st.gt.c1 = cj; st.gt.c2 = ck;
          st.gt.w_coeff[0] = coeff0;
          st.gt.w_coeff[1] = coeff1;
          st.gt.w_coeff[2] = coeff2;
          st.gt.weighted_defect_upper = total_defect;
          st.gt.min_p = std::min({p0, p1, p2});
          st.defect_sum = total_defect;
          st.min_p = st.gt.min_p;
          sortable.push_back(st);
        }
      }
    }
  }

  if (sort_triples) {
    std::sort(sortable.begin(), sortable.end(),
              [](const SortableTriple &a, const SortableTriple &b) {
                if (std::abs(a.defect_sum - b.defect_sum) > 1e-9)
                  return a.defect_sum < b.defect_sum;
                return a.min_p > b.min_p;
              });

    pool->gpu_triples.reserve(sortable.size());
    for (const auto &st : sortable) {
      pool->gpu_triples.push_back(st.gt);
    }
  }
  pool->gpu_triples.shrink_to_fit();
  return pool;
}

// Zigzag integer encoding:
// zz(n) = (n >= 0) ? (2 * n) : (2 * (-n) - 1)
static inline std::string ZigzagRatString(const BigRat &r) {
  BigInt num = r.Numerator();
  BigInt den = r.Denominator();
  BigInt zz = (num >= 0) ? (num * 2) : ((-num) * 2 - 1);
  return zz.ToString() + "," + den.ToString();
}

static inline std::string ZigzagIntString(int64_t n) {
  int64_t zz = (n >= 0) ? (2 * n) : (2 * (-n) - 1);
  return std::to_string(zz);
}

struct AxisCertificateQ {
  int edge_start[3];
  int edge_finish[3];
  int edge_start2[3];
  int edge_finish2[3];
  int mix[3];
  int support_index[3];
  int nonzero_witness[3];
  BigRat B;
};

struct ParsedNode {
  int64_t id = -1;
  int64_t parent_id = -1;
  int depth = 0;
  int view_depth = 0;
  std::string tag;
  int winning_triple = -1;
  double margin = 0.0;
  int inner[3] = {0, 0, 0};
  int fund_dir = 1;
  double tube_radius = 0.0;

  int shared_index = 0; // 0..3 (which sub-wedge)
  std::vector<int64_t> children;
  int64_t child_ids[4] = {-1, -1, -1, -1};
  CayleyBoxQ box;
  TriangleQ tri;

  AxisCertificateQ axis_cert;
  BigRat ball_multiplier = BigRat(0);
};

static Vec3Q GetExactEdge(const ContactInfo &c) {
  Vec3Q v_start = {VERTICES_Q[c.edge_start][0], VERTICES_Q[c.edge_start][1], VERTICES_Q[c.edge_start][2]};
  Vec3Q v_finish = {VERTICES_Q[c.edge_finish][0], VERTICES_Q[c.edge_finish][1], VERTICES_Q[c.edge_finish][2]};
  Vec3Q v_start2 = {VERTICES_Q[c.edge_start2][0], VERTICES_Q[c.edge_start2][1], VERTICES_Q[c.edge_start2][2]};
  Vec3Q v_finish2 = {VERTICES_Q[c.edge_finish2][0], VERTICES_Q[c.edge_finish2][1], VERTICES_Q[c.edge_finish2][2]};
  BigRat mixQ = BigRat(c.mix, 1000);
  return (v_start - v_finish) * mixQ + (v_start2 - v_finish2) * (BigRat(1) - mixQ);
}

static bool ComputeExactCertificate(ParsedNode *node,
                                   const std::shared_ptr<const TrianglePool> &pool) {
  if (node->winning_triple < 0 || node->winning_triple >= pool->gpu_triples.size()) {
    std::cerr << "\nError: node " << node->id << " depth " << node->depth
              << " winning_triple " << node->winning_triple
              << " exceeds pool size " << pool->gpu_triples.size()
              << " (contacts=" << pool->contacts.size() << ")\n";
    return false;
  }
  const auto &gt = pool->gpu_triples[node->winning_triple];

  const ContactInfo *contacts[3] = {
    &pool->contacts[gt.c0],
    &pool->contacts[gt.c1],
    &pool->contacts[gt.c2]
  };

  Vec3Q edges[3] = {
    GetExactEdge(*contacts[0]),
    GetExactEdge(*contacts[1]),
    GetExactEdge(*contacts[2])
  };

  Vec3Q coeff0 = Vec3Q::Cross(edges[1], edges[2]);
  Vec3Q coeff1 = Vec3Q::Cross(edges[2], edges[0]);
  Vec3Q coeff2 = Vec3Q::Cross(edges[0], edges[1]);

  // Check probe weights at corner 0
  BigRat p0 = Vec3Q::Dot(node->tri.corners[0], coeff0);
  BigRat p1 = Vec3Q::Dot(node->tri.corners[0], coeff1);
  BigRat p2 = Vec3Q::Dot(node->tri.corners[0], coeff2);

  if (p0 < 0 && p1 < 0 && p2 < 0) {
    std::swap(contacts[1], contacts[2]);
    std::swap(edges[1], edges[2]);
    std::swap(node->inner[1], node->inner[2]);
    coeff0 = Vec3Q::Cross(edges[1], edges[2]);
    coeff1 = Vec3Q::Cross(edges[2], edges[0]);
    coeff2 = Vec3Q::Cross(edges[0], edges[1]);
  }

  Vec3Q w_coeffs[3] = {coeff0, coeff1, coeff2};
  BigRat support_error = BigRat(6, 1000000000000000LL); // 6 / 10^15
  BigRat sum_weight_upper(0);

  for (int m = 0; m < 3; m++) {
    BigRat w_max = Vec3Q::Dot(node->tri.corners[0], w_coeffs[m]);
    BigRat w_min = w_max;
    for (int c = 1; c < 3; c++) {
      BigRat val = Vec3Q::Dot(node->tri.corners[c], w_coeffs[m]);
      if (val > w_max) w_max = val;
      if (val < w_min) w_min = val;
    }
    if (w_min < support_error) {
      // In Lean, weightLower = min3(w) - support_error.
      // If min3(w) < support_error, weightLower < 0, violating weight_nonneg!
      return false;
    }
    sum_weight_upper = sum_weight_upper + w_max + support_error;
  }

  // Exact B = ceil_to(2 * sum_weight_upper, 10^9)
  BigRat exact_B = sum_weight_upper * BigRat(2);
  BigRat denom_B(1000000000LL);
  BigInt scaled_B = (exact_B * denom_B).Numerator();
  BigInt div_B = (exact_B * denom_B).Denominator();
  BigInt ceil_val = (scaled_B + div_B - 1) / div_B;
  BigRat B = BigRat(ceil_val, denom_B.Numerator());

  AxisCertificateQ cert;
  cert.B = B;

  for (int m = 0; m < 3; m++) {
    cert.edge_start[m] = contacts[m]->edge_start;
    cert.edge_finish[m] = contacts[m]->edge_finish;
    cert.edge_start2[m] = contacts[m]->edge_start2;
    cert.edge_finish2[m] = contacts[m]->edge_finish2;
    cert.mix[m] = contacts[m]->mix;
    cert.support_index[m] = contacts[m]->vertex;

    int sel = contacts[m]->vertex;
    Vec3Q v_sel = {VERTICES_Q[sel][0], VERTICES_Q[sel][1], VERTICES_Q[sel][2]};
    BigRat best_support = BigRat(1000000);
    int best_w = -1;

    for (int k = 0; k < 20; k++) {
      bool tie = (k == sel) ||
                 (contacts[m]->mix == 1000 && sel == contacts[m]->edge_finish && k == contacts[m]->edge_start) ||
                 (contacts[m]->mix == 0 && sel == contacts[m]->edge_start2 && k == contacts[m]->edge_finish2);
      BigRat s_upper(0);
      if (!tie) {
        Vec3Q v_k = {VERTICES_Q[k][0], VERTICES_Q[k][1], VERTICES_Q[k][2]};
        Vec3Q delta = v_k - v_sel;
        Vec3Q s_coeff = Vec3Q::Cross(edges[m], delta);
        BigRat max_s = Vec3Q::Dot(node->tri.corners[0], s_coeff);
        for (int c = 1; c < 3; c++) {
          BigRat val = Vec3Q::Dot(node->tri.corners[c], s_coeff);
          if (val > max_s) max_s = val;
        }
        s_upper = max_s + support_error;
      }
      if (s_upper < best_support) {
        best_support = s_upper;
        best_w = k;
      }
    }
    cert.nonzero_witness[m] = best_w;
    if (best_support >= 0) {
      std::cerr << "Warning: node " << node->id << " contact " << m
                << " best_support >= 0: " << best_support.ToString() << "\n";
      return false;
    }
  }

  node->axis_cert = cert;
  node->ball_multiplier = BigRat(0);
  return true;
}

int main(int argc, char **argv) {
  int chart = 0;
  std::string in_path;
  std::string out_path;
  int cone_samples = 8;
  int escalate_depth = 36;
  int escalate_cone_samples = 12;
  bool fill_pending = false;
  bool legacy_sort = false;

  std::vector<std::string> positional;
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--cone_samples" && i + 1 < argc) {
      cone_samples = std::atoi(argv[++i]);
    } else if (arg == "--escalate_depth" && i + 1 < argc) {
      escalate_depth = std::atoi(argv[++i]);
    } else if (arg == "--escalate_cone_samples" && i + 1 < argc) {
      escalate_cone_samples = std::atoi(argv[++i]);
    } else if (arg == "--fill_pending" || arg == "--fill-pending") {
      fill_pending = true;
    } else if (arg == "--legacy_sort" || arg == "--legacy-sort") {
      legacy_sort = true;
    } else if (arg[0] != '-') {
      positional.push_back(arg);
    }
  }

  if (positional.size() > 0) chart = std::atoi(positional[0].c_str());
  if (positional.size() > 1) in_path = positional[1];
  else in_path = "chart" + std::to_string(chart) + ".rows.log";
  if (positional.size() > 2) out_path = positional[2];
  else out_path = "chart" + std::to_string(chart) + ".pack";

  std::cout << ACYAN("=== Nopert #229 Certificate Packer ===") << "\n";
  std::cout << "Chart:                 " << chart << "\n";
  std::cout << "Input log:             " << in_path << "\n";
  std::cout << "Output pack:           " << out_path << "\n";
  std::cout << "Cone samples:          " << cone_samples << "\n";
  std::cout << "Escalate depth:        " << escalate_depth << "\n";
  std::cout << "Escalate cone samples: " << escalate_cone_samples << "\n";
  std::cout << "Fill pending:          " << (fill_pending ? "yes" : "no") << "\n";
  std::cout << "Legacy triple sort:    " << (legacy_sort ? "yes (pre-r7377)" : "no (natural r7377+)") << "\n\n";

  Timer total_timer;
  std::cout << "Reading " << in_path << "..." << std::flush;
  std::ifstream infile(in_path);
  if (!infile.is_open()) {
    std::cerr << ARED("\nError: Could not open ") << in_path << "\n";
    return 1;
  }

  std::unordered_map<int64_t, ParsedNode> nodes;
  std::vector<int64_t> all_node_ids;

  // Root node 0
  ParsedNode root;
  root.id = 0;
  root.depth = 0;
  root.view_depth = 0;
  root.box.center = {BigRat(0), BigRat(0), BigRat(0)};
  if (chart == 0) root.box.radii = {BigRat(1), BigRat(1), BigRat(1, 3)};
  else if (chart == 1) root.box.radii = {BigRat(1), BigRat(1, 3), BigRat(1)};
  else if (chart == 2) root.box.radii = {BigRat(1, 3), BigRat(1), BigRat(1)};
  else root.box.radii = {BigRat(1), BigRat(1), BigRat(1)};

  root.tri.corners[0] = {BigRat(1), BigRat(0), BigRat(0)};
  root.tri.corners[1] = {BigRat(10, 41), BigRat(31, 41), BigRat(0)};
  root.tri.corners[2] = {BigRat(0), BigRat(0), BigRat(1)};
  nodes[0] = root;
  all_node_ids.push_back(0);

  std::string line;
  int64_t cert_count = 0;
  while (std::getline(infile, line)) {
    if (line.empty()) continue;
    std::istringstream iss(line);
    std::string tag;
    int64_t id, parent_id;
    int depth;
    if (!(iss >> tag >> id >> parent_id >> depth)) continue;

    ParsedNode node;
    node.id = id;
    node.parent_id = parent_id;
    node.depth = depth;

    if (tag == "CE" || tag == "CERT") {
      node.tag = "CERT";
      cert_count++;
      iss >> node.winning_triple >> node.margin >> node.inner[0] >> node.inner[1] >> node.inner[2];
    } else if (tag == "PR" || tag == "PRUNE") {
      std::string prune_kind;
      iss >> prune_kind;
      if (prune_kind == "RADIUS" || prune_kind == "RA") {
        node.tag = "PRUNE_RADIUS";
      } else if (prune_kind == "FUNDAMENTAL" || prune_kind == "FU") {
        node.tag = "PRUNE_FUNDAMENTAL";
        iss >> node.fund_dir;
      }
    } else if (tag == "TU" || tag == "TUBE") {
      node.tag = "TUBE";
      iss >> node.tube_radius;
    } else if (tag == "SP" || tag == "SPLIT") {
      node.tag = "SPLIT";
      int64_t c0, c1;
      if (iss >> c0 >> c1) {
        node.child_ids[0] = c0;
        node.child_ids[1] = c1;
      }
    } else if (tag == "SO" || tag == "SPLIT_ORIGIN") {
      node.tag = "SPLIT_ORIGIN";
      int64_t c0, c1;
      if (iss >> c0 >> c1) {
        node.child_ids[0] = c0;
        node.child_ids[1] = c1;
      }
    } else if (tag == "SV" || tag == "SPLIT_VIEW") {
      node.tag = "SPLIT_VIEW";
      int64_t c0, c1, c2, c3;
      if (iss >> c0 >> c1 >> c2 >> c3) {
        node.child_ids[0] = c0;
        node.child_ids[1] = c1;
        node.child_ids[2] = c2;
        node.child_ids[3] = c3;
      }
    } else if (tag == "DF" || tag == "DI" || tag == "DIFFICULT") {
      node.tag = "DIFFICULT";
      iss >> node.margin;
    } else {
      node.tag = tag;
    }

    nodes[id] = node;
    nodes[parent_id].children.push_back(id);
    all_node_ids.push_back(id);
  }
  std::cout << " done (" << nodes.size() << " nodes, " << cert_count << " certificates) in "
            << total_timer.Seconds() << "s.\n";

  // Reconstruct tree and geometry
  std::cout << "Reconstructing search tree..." << std::flush;
  auto root_sub_wedges = root.tri.Subdivide();

  // Assign the 4 root children to the 4 sub-wedges
  int64_t root_c[4];
  if (nodes[0].child_ids[0] > 0 && nodes[0].child_ids[1] > 0 &&
      nodes[0].child_ids[2] > 0 && nodes[0].child_ids[3] > 0) {
    for (int i = 0; i < 4; i++) root_c[i] = nodes[0].child_ids[i];
  } else if (nodes[0].children.size() == 4) {
    for (int i = 0; i < 4; i++) root_c[i] = nodes[0].children[i];
  } else {
    for (int i = 0; i < 4; i++) root_c[i] = i + 1;
  }

  for (int i = 0; i < 4; i++) {
    int64_t cid = root_c[i];
    nodes[cid].id = cid;
    nodes[cid].box = root.box;
    nodes[cid].tri = root_sub_wedges[i];
    nodes[cid].shared_index = i;
    nodes[cid].view_depth = 1;
  }

  // BFS propagate boxes and triangles down the tree
  std::vector<int64_t> bfs_queue;
  for (int64_t cid : root_c) bfs_queue.push_back(cid);

  size_t q_head = 0;
  while (q_head < bfs_queue.size()) {
    int64_t curr_id = bfs_queue[q_head++];
    const auto &curr = nodes[curr_id];
    if (curr.children.empty()) continue;

    if (curr.tag == "SPLIT" || curr.tag == "SPLIT_ORIGIN") {
      int axis = curr.box.WidestAxis();
      int64_t c0 = curr.child_ids[0];
      int64_t c1 = curr.child_ids[1];
      if (c0 < 0 || c1 < 0) {
        if (curr.children.size() >= 2) {
          c0 = std::min(curr.children[0], curr.children[1]);
          c1 = std::max(curr.children[0], curr.children[1]);
          nodes[curr_id].child_ids[0] = c0;
          nodes[curr_id].child_ids[1] = c1;
        } else if (curr.children.size() == 1) {
          c0 = curr.children[0];
          nodes[curr_id].child_ids[0] = c0;
        }
      }
      for (int64_t child_id : {c0, c1}) {
        if (child_id < 0) continue;
        bool is_upper = (child_id == c1 && c0 != c1);
        nodes[child_id].tri = curr.tri;
        nodes[child_id].shared_index = curr.shared_index;
        nodes[child_id].box = curr.box;
        nodes[child_id].box.radii = curr.box.radii;
        if (axis == 0) nodes[child_id].box.radii.x = curr.box.radii.x / BigRat(2);
        if (axis == 1) nodes[child_id].box.radii.y = curr.box.radii.y / BigRat(2);
        if (axis == 2) nodes[child_id].box.radii.z = curr.box.radii.z / BigRat(2);
        nodes[child_id].box.center = curr.box.center;
        if (is_upper) {
          if (axis == 0) nodes[child_id].box.center.x += nodes[child_id].box.radii.x;
          if (axis == 1) nodes[child_id].box.center.y += nodes[child_id].box.radii.y;
          if (axis == 2) nodes[child_id].box.center.z += nodes[child_id].box.radii.z;
        } else {
          if (axis == 0) nodes[child_id].box.center.x -= nodes[child_id].box.radii.x;
          if (axis == 1) nodes[child_id].box.center.y -= nodes[child_id].box.radii.y;
          if (axis == 2) nodes[child_id].box.center.z -= nodes[child_id].box.radii.z;
        }
        bfs_queue.push_back(child_id);
      }
    } else if (curr.tag == "SPLIT_VIEW") {
      auto sub_tris = curr.tri.Subdivide();
      bool have_ids = (curr.child_ids[0] >= 0 && curr.child_ids[1] >= 0 &&
                       curr.child_ids[2] >= 0 && curr.child_ids[3] >= 0);
      if (have_ids) {
        for (int t = 0; t < 4; t++) {
          int64_t child_id = curr.child_ids[t];
          nodes[child_id].box = curr.box;
          nodes[child_id].shared_index = curr.shared_index;
          nodes[child_id].tri = sub_tris[t];
          bfs_queue.push_back(child_id);
        }
      } else {
        auto sorted_children = curr.children;
        std::sort(sorted_children.begin(), sorted_children.end());
        if (sorted_children.size() == 4) {
          nodes[curr_id].child_ids[0] = sorted_children[3];
          nodes[curr_id].child_ids[1] = sorted_children[2];
          nodes[curr_id].child_ids[2] = sorted_children[1];
          nodes[curr_id].child_ids[3] = sorted_children[0];
          for (int t = 0; t < 4; t++) {
            int64_t child_id = nodes[curr_id].child_ids[t];
            nodes[child_id].box = curr.box;
            nodes[child_id].shared_index = curr.shared_index;
            nodes[child_id].tri = sub_tris[t];
            bfs_queue.push_back(child_id);
          }
        } else {
          for (size_t i = 0; i < sorted_children.size() && i < 4; i++) {
            int64_t child_id = sorted_children[i];
            int t = (sorted_children.size() == 4) ? (3 - i) : 0;
            nodes[curr_id].child_ids[t] = child_id;
            nodes[child_id].box = curr.box;
            nodes[child_id].shared_index = curr.shared_index;
            nodes[child_id].tri = sub_tris[t];
            bfs_queue.push_back(child_id);
          }
        }
      }
    }
  }
  std::cout << " done.\n";

  // Build triangle pools and compute exact certificates
  std::cout << "Computing exact rational certificates...\n";
  std::unordered_map<std::string, std::shared_ptr<const TrianglePool>> pool_cache;

  auto get_tri_key = [](const TriangleQ &tri) -> std::string {
    return tri.corners[0].x.ToString() + "_" + tri.corners[0].y.ToString() + "_" + tri.corners[0].z.ToString() + "|" +
           tri.corners[1].x.ToString() + "_" + tri.corners[1].y.ToString() + "_" + tri.corners[1].z.ToString() + "|" +
           tri.corners[2].x.ToString() + "_" + tri.corners[2].y.ToString() + "_" + tri.corners[2].z.ToString();
  };

  int64_t certs_done = 0;
  for (int64_t id : all_node_ids) {
    auto &node = nodes[id];
    if (node.tag != "CERT") continue;

    int samples = (escalate_depth > 0 && node.depth >= escalate_depth) ? escalate_cone_samples : cone_samples;
    std::string key = get_tri_key(node.tri) + "@" + std::to_string(samples);
    auto it = pool_cache.find(key);
    if (it == pool_cache.end()) {
      vec3d tri_d[3];
      for (int c = 0; c < 3; c++) {
        tri_d[c].x = node.tri.corners[c].x.ToDouble();
        tri_d[c].y = node.tri.corners[c].y.ToDouble();
        tri_d[c].z = node.tri.corners[c].z.ToDouble();
      }
      auto pool = BuildTrianglePool(tri_d, samples, legacy_sort);
      it = pool_cache.emplace(key, pool).first;
    }

    if (!ComputeExactCertificate(&node, it->second)) {
      std::cerr << ARED("Failed to verify certificate for node ") << id << "\n";
      return 1;
    }
    certs_done++;
    if (certs_done % 1000 == 0 || certs_done == cert_count) {
      std::cout << "\rVerified " << certs_done << " / " << cert_count << " certificates..." << std::flush;
    }
  }
  std::cout << "\nAll " << cert_count << " certificates verified successfully!\n";

  // Deduplicate Intervals and Triangles
  std::cout << "Deduplicating intervals and triangles..." << std::flush;
  std::vector<CayleyBoxQ> intervals;
  std::unordered_map<std::string, int> interval_map;
  auto get_interval_id = [&](const CayleyBoxQ &box) -> int {
    std::string key = box.center.x.ToString() + "," + box.center.y.ToString() + "," + box.center.z.ToString() + ";" +
                      box.radii.x.ToString() + "," + box.radii.y.ToString() + "," + box.radii.z.ToString();
    auto it = interval_map.find(key);
    if (it != interval_map.end()) return it->second;
    int idx = intervals.size();
    intervals.push_back(box);
    interval_map[key] = idx;
    return idx;
  };

  std::vector<TriangleQ> triangles;
  std::unordered_map<std::string, int> triangle_map;
  auto get_triangle_id = [&](const TriangleQ &tri) -> int {
    std::string key = get_tri_key(tri);
    auto it = triangle_map.find(key);
    if (it != triangle_map.end()) return it->second;
    int idx = triangles.size();
    triangles.push_back(tri);
    triangle_map[key] = idx;
    return idx;
  };

  // Root interval & root triangle
  int root_interval_idx = get_interval_id(root.box);
  int root_triangle_idx = get_triangle_id(root.tri);

  // Collect indices for all nodes
  for (int64_t id : all_node_ids) {
    get_interval_id(nodes[id].box);
    get_triangle_id(nodes[id].tri);
  }
  std::cout << " done (" << intervals.size() << " intervals, " << triangles.size() << " triangles).\n";

  // Write packed wire format:
  // Row 0: viewRoot (child = 1)
  // Row 1: viewSplit (root 0, UPPER_WEDGE_PROJECTIVE_ROOT, children = [sub_wedges 0..3])
  // Rows 2..N: search nodes 1..N-1 mapped to Lean ID = node.id + 1
  std::cout << "Writing packed output to " << out_path << "..." << std::flush;
  std::ofstream outfile(out_path, std::ios::binary);
  if (!outfile.is_open()) {
    std::cerr << "\nError: Could not open " << out_path << " for writing!\n";
    return 1;
  }

  int64_t max_id = 0;
  for (const auto &[id, node] : nodes) {
    if (id > max_id) max_id = id;
    for (int64_t cid : node.child_ids) {
      if (cid > max_id) max_id = cid;
    }
  }

  int64_t total_lean_rows = max_id + 2;

  // Header: total_rows, interval_count, [intervals], triangle_count, [triangles]
  outfile << total_lean_rows << "," << intervals.size();
  for (const auto &box : intervals) {
    outfile << "," << ZigzagRatString(box.center.x)
            << "," << ZigzagRatString(box.center.y)
            << "," << ZigzagRatString(box.center.z)
            << "," << ZigzagRatString(box.radii.x)
            << "," << ZigzagRatString(box.radii.y)
            << "," << ZigzagRatString(box.radii.z);
  }

  outfile << "," << triangles.size();
  for (const auto &tri : triangles) {
    for (int c = 0; c < 3; c++) {
      outfile << "," << ZigzagRatString(tri.corners[c].x)
              << "," << ZigzagRatString(tri.corners[c].y)
              << "," << ZigzagRatString(tri.corners[c].z);
    }
  }

  // Row 0: viewRoot
  // Tag 0, id 0, intervalIndex, child 1
  outfile << ",0,0," << root_interval_idx << ",1";

  // Row 1: viewSplit
  // Tag 2, id 1, intervalIndex, c0, c1, c2, c3, root 0, triangleIndex
  // Find node IDs for sub-wedges 0, 1, 2, 3:
  int64_t wedge_child_lean[4] = {0, 0, 0, 0};
  for (int64_t cid : root_c) {
    wedge_child_lean[nodes[cid].shared_index] = cid + 1;
  }
  outfile << ",2,1," << root_interval_idx
          << "," << wedge_child_lean[0]
          << "," << wedge_child_lean[1]
          << "," << wedge_child_lean[2]
          << "," << wedge_child_lean[3]
          << ",0," << root_triangle_idx;

  for (int64_t lean_id = 2; lean_id < total_lean_rows; lean_id++) {
    int64_t id = lean_id - 1;
    auto it = nodes.find(id);
    if (it == nodes.end() || it->second.tag.empty()) {
      if (fill_pending) {
        outfile << ",0," << lean_id << "," << root_interval_idx << "," << lean_id;
        continue;
      } else {
        std::cerr << "\nError: Missing node " << id << " in tree. Pass --fill_pending to pack anyway for testing.\n";
        return 1;
      }
    }
    const auto &node = it->second;
    int iv_idx = get_interval_id(node.box);
    int tri_idx = get_triangle_id(node.tri);

    if (node.tag == "SPLIT" || node.tag == "SPLIT_ORIGIN") {
      if (node.child_ids[0] < 0 || node.child_ids[1] < 0) {
        if (fill_pending) {
          outfile << ",0," << lean_id << "," << iv_idx << "," << lean_id;
          continue;
        } else {
          std::cerr << "\nError: Node " << id << " has tag " << node.tag
                    << " but missing children (unfilled/incomplete tree). Pass --fill_pending to pack anyway for testing.\n";
          return 1;
        }
      }
      int64_t c0 = node.child_ids[0];
      int64_t c1 = node.child_ids[1];
      int axis = node.box.WidestAxis();
      int coord = axis + 2; // In Lean: x=2, y=3, z=4
      outfile << ",1," << lean_id << "," << iv_idx
              << "," << (c0 + 1) << "," << (c1 + 1)
              << "," << coord << ",0," << tri_idx;

    } else if (node.tag == "SPLIT_VIEW") {
      if (node.child_ids[0] < 0 || node.child_ids[1] < 0 ||
          node.child_ids[2] < 0 || node.child_ids[3] < 0) {
        if (fill_pending) {
          outfile << ",0," << lean_id << "," << iv_idx << "," << lean_id;
          continue;
        } else {
          std::cerr << "\nError: Node " << id << " has tag SPLIT_VIEW but missing children.\n";
          return 1;
        }
      }
      int64_t c0 = node.child_ids[0];
      int64_t c1 = node.child_ids[1];
      int64_t c2 = node.child_ids[2];
      int64_t c3 = node.child_ids[3];
      outfile << ",2," << lean_id << "," << iv_idx
              << "," << (c0 + 1)
              << "," << (c1 + 1)
              << "," << (c2 + 1)
              << "," << (c3 + 1)
              << ",0," << tri_idx;

    } else if (node.tag.empty() || node.tag == "DIFFICULT") {
      if (fill_pending) {
        outfile << ",0," << lean_id << "," << iv_idx << "," << lean_id;
        continue;
      } else {
        std::cerr << "\nError: " << (node.tag == "DIFFICULT" ? "Difficult" : "Empty")
                  << " node " << id << "\n";
        return 1;
      }

    } else if (node.tag == "CERT") {
      outfile << ",4," << lean_id << "," << iv_idx << ",0," << tri_idx;
      // 23 integers of AxisCertificate:
      const auto &c = node.axis_cert;
      for (int m = 0; m < 3; m++) outfile << "," << c.edge_start[m];
      for (int m = 0; m < 3; m++) outfile << "," << c.edge_finish[m];
      for (int m = 0; m < 3; m++) outfile << "," << c.edge_start2[m];
      for (int m = 0; m < 3; m++) outfile << "," << c.edge_finish2[m];
      for (int m = 0; m < 3; m++) outfile << "," << c.mix[m];
      for (int m = 0; m < 3; m++) outfile << "," << c.support_index[m];
      for (int m = 0; m < 3; m++) outfile << "," << c.nonzero_witness[m];
      outfile << "," << ZigzagRatString(c.B);
      // inner vertices:
      outfile << "," << node.inner[0] << "," << node.inner[1] << "," << node.inner[2];
      // ball multiplier:
      outfile << "," << ZigzagRatString(node.ball_multiplier);

    } else if (node.tag == "TUBE") {
      outfile << ",6," << lean_id << "," << iv_idx
              << ",0," << ZigzagRatString(BigRat(std::to_string(node.tube_radius)))
              << "," << node.shared_index
              << ",0," << tri_idx;

    } else if (node.tag == "PRUNE_RADIUS") {
      outfile << ",7," << lean_id << "," << iv_idx
              << ",0," << tri_idx;

    } else if (node.tag == "PRUNE_FUNDAMENTAL") {
      int dir = (node.fund_dir == 1) ? 1 : 0;
      outfile << ",8," << lean_id << "," << iv_idx
              << "," << dir
              << ",0," << tri_idx;
    }
  }

  outfile.close();
  std::cout << " done!\n\n"
            << ANSI_GREEN << "Packed " << total_lean_rows << " Lean rows into " << out_path
            << ANSI_RESET << " in " << total_timer.Seconds() << "s.\n";
  return 0;
}
