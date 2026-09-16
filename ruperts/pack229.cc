// pack229.cc: Converts lean229 proof logs to Lean 4 packed format (.pack).
// Supports standard exact rational certificates (CE) and mixed certificates (MX).
//
// Automatically merges resolved difficult cells from chart<chart>.<id>.done files
// with collision-safe node renumbering and streaming low-memory verification.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "bignum/big.h"
#include "bignum/big-overloads.h"
#include "geom/hull-2d.h"
#include "timer.h"
#include "yocto-math.h"

// Exact rational vertex coordinates for Noperthedron #229
static const BigRat VERTICES_Q[20][3] = {
  {BigRat("2142036603832375/50000000000000000"), BigRat("28403317780935653/50000000000000000"), BigRat("14120418315444177/25000000000000000")},
  {BigRat("-171094052819828/1000000000000000"), BigRat("46920848781758567/50000000000000000"), BigRat("3001672949030625/10000000000000000")},
  {BigRat("-2791996671138589/10000000000000000"), BigRat("8916783831939151/10000000000000000"), BigRat("-420605170858861/10000000000000000")},
  {BigRat("2906058497811437/50000000000000000"), BigRat("602579091333187/1000000000000000"), BigRat("-382169975802723/500000000000000")},
  {BigRat("-5270246949360691/10000000000000000"), BigRat("10814305761158757/50000000000000000"), BigRat("14120418315444177/25000000000000000")},
  {BigRat("-18907170992752637/20000000000000000"), BigRat("6363333972378213/50000000000000000"), BigRat("3001672949030625/10000000000000000")},
  {BigRat("-9343139787381105/10000000000000000"), BigRat("100091111676232/10000000000000000"), BigRat("-420605170858861/10000000000000000")},
  {BigRat("-5551263421462106/10000000000000000"), BigRat("1207418485492689/5000000000000000"), BigRat("-382169975802723/500000000000000")},
  {BigRat("-1842799532288414/5000000000000000"), BigRat("-21719709255805741/50000000000000000"), BigRat("14120418315444177/25000000000000000")},
  {BigRat("-4131696624115331/10000000000000000"), BigRat("-42988092105061941/50000000000000000"), BigRat("3001672949030625/10000000000000000")},
  {BigRat("-7455953197761/25000000000000"), BigRat("-8854924122951479/10000000000000000"), BigRat("-420605170858861/10000000000000000")},
  {BigRat("-2006040587264951/5000000000000000"), BigRat("-4533339587973062/10000000000000000"), BigRat("-382169975802723/500000000000000")},
  {BigRat("1496210729273696/5000000000000000"), BigRat("-24237824307012397/50000000000000000"), BigRat("14120418315444177/25000000000000000")},
  {BigRat("6900056551469845/10000000000000000"), BigRat("-3293143600481752/5000000000000000"), BigRat("3001672949030625/10000000000000000")},
  {BigRat("7499926789483198/10000000000000000"), BigRat("-5572735187461599/10000000000000000"), BigRat("-420605170858861/10000000000000000")},
  {BigRat("1535830444989514/5000000000000000"), BigRat("-2608297459449088/5000000000000000"), BigRat("-382169975802723/500000000000000")},
  {BigRat("5535017234623651/10000000000000000"), BigRat("6739910020723709/50000000000000000"), BigRat("14120418315444177/25000000000000000")},
  {BigRat("8396166097220085/10000000000000000"), BigRat("4527069071148534/10000000000000000"), BigRat("3001672949030625/10000000000000000")},
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

struct vec3d {
  double x, y, z;
  vec3d operator+(const vec3d &o) const { return {x + o.x, y + o.y, z + o.z}; }
  vec3d operator-(const vec3d &o) const { return {x - o.x, y - o.y, z - o.z}; }
  vec3d operator*(double s) const { return {x * s, y * s, z * s}; }
  vec3d operator/(double s) const { return {x / s, y / s, z / s}; }
};
inline vec3d operator*(double s, const vec3d &v) { return v * s; }
inline double dot(const vec3d &a, const vec3d &b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
inline vec3d cross(const vec3d &a, const vec3d &b) {
  return {a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x};
}
inline double length(const vec3d &v) { return std::sqrt(dot(v, v)); }
inline vec3d normalize(const vec3d &v) { double l = length(v); return l > 0 ? v / l : v; }

struct Vec3Q {
  BigRat x, y, z;
  Vec3Q() : x(0), y(0), z(0) {}
  Vec3Q(BigRat x, BigRat y, BigRat z) : x(x), y(y), z(z) {}

  vec3d ToVec3D() const { return {x.ToDouble(), y.ToDouble(), z.ToDouble()}; }

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

inline void AccumulateContactPoly(double C[10], double weight, vec3d u,
                                  vec3d vin, vec3d vout, vec3d s) {
  double sx = s.x, sy = s.y, sz = s.z;
  double ux = u.x, uy = u.y, uz = u.z;
  double vx = vin.x, vy = vin.y, vz = vin.z;
  double ox = vout.x, oy = vout.y, oz = vout.z;

  C[0] += weight * (ux * (sx*vx - ox) + uy * (sy*vy - oy) + uz * (sz*vz - oz));
  C[1] += weight * 2.0 * (-uy * sy * vz + uz * sz * vy);
  C[2] += weight * 2.0 * (ux * sx * vz - uz * sz * vx);
  C[3] += weight * 2.0 * (-ux * sx * vy + uy * sy * vx);
  C[4] += weight * (ux * (sx*vx - ox) - uy * (sy*vy + oy) - uz * (sz*vz + oz));
  C[7] += weight * (-ux * (sx*vx + ox) + uy * (sy*vy - oy) - uz * (sz*vz + oz));
  C[9] += weight * (-ux * (sx*vx + ox) - uy * (sy*vy + oy) + uz * (sz*vz - oz));
  C[5] += weight * 2.0 * (ux * sx * vy + uy * sy * vx);
  C[6] += weight * 2.0 * (ux * sx * vz + uz * sz * vx);
  C[8] += weight * 2.0 * (uy * sy * vz + uz * sz * vy);
}


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
      vec3d e = lam * first + (1.0 - lam) * second;

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

  vec3d view = (tri_corners[0] + tri_corners[1] + tri_corners[2]) / 3.0;

  for (int i = 0; i < C; i++) {
    for (int j = i + 1; j < C; j++) {
      for (int k = j + 1; k < C; k++) {
        vec3d e0 = pool->contacts[i].edge;
        vec3d e1 = pool->contacts[j].edge;
        vec3d e2 = pool->contacts[k].edge;

        vec3d coeff0 = cross(e1, e2);
        vec3d coeff1 = cross(e2, e0);
        vec3d coeff2 = cross(e0, e1);

        double p0 = dot(view, coeff0);
        double p1 = dot(view, coeff1);
        double p2 = dot(view, coeff2);

        int ci = i, cj = j, ck = k;
        if (p0 < 0.0 && p1 < 0.0 && p2 < 0.0) {
          std::swap(cj, ck);
          std::swap(e1, e2);
          coeff0 = cross(e1, e2);
          coeff1 = cross(e2, e0);
          coeff2 = cross(e0, e1);
          p0 = dot(view, coeff0);
          p1 = dot(view, coeff1);
          p2 = dot(view, coeff2);
        }

        if (p0 <= 1e-9 || p1 <= 1e-9 || p2 <= 1e-9) continue;

        double w0_min = std::min({dot(tri_corners[0], coeff0), dot(tri_corners[1], coeff0), dot(tri_corners[2], coeff0)});
        double w1_min = std::min({dot(tri_corners[0], coeff1), dot(tri_corners[1], coeff1), dot(tri_corners[2], coeff1)});
        double w2_min = std::min({dot(tri_corners[0], coeff2), dot(tri_corners[1], coeff2), dot(tri_corners[2], coeff2)});
        if (w0_min <= 1e-11 || w1_min <= 1e-11 || w2_min <= 1e-11) continue;


        GpuTriple gt;
        gt.c0 = ci;
        gt.c1 = cj;
        gt.c2 = ck;
        gt.w_coeff[0] = coeff0;
        gt.w_coeff[1] = coeff1;
        gt.w_coeff[2] = coeff2;
        gt.min_p = std::min({p0, p1, p2});
        gt.weighted_defect_upper = ComputeWeightedDefectUpper(tri_corners, pool->contacts, ci, cj, ck, gt.w_coeff);

        if (sort_triples) {
          SortableTriple st;
          st.gt = gt;
          st.defect_sum = pool->contacts[ci].defect + pool->contacts[cj].defect + pool->contacts[ck].defect;
          st.min_p = gt.min_p;
          sortable.push_back(st);
        } else {
          pool->gpu_triples.push_back(gt);
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

struct AxisCertificateQ {
  int edge_start[3] = {0, 0, 0};
  int edge_finish[3] = {0, 0, 0};
  int edge_start2[3] = {0, 0, 0};
  int edge_finish2[3] = {0, 0, 0};
  int mix[3] = {0, 0, 0};
  int support_index[3] = {0, 0, 0};
  int nonzero_witness[3] = {0, 0, 0};
  BigRat B;
};

struct StoredCert {
  AxisCertificateQ axis_cert;
  int inner[3] = {0, 0, 0};
  BigRat ball_multiplier = BigRat(0);
};

struct StoredMixedComponent {
  int winning_triple = -1;
  int inner[3] = {0, 0, 0};
  AxisCertificateQ axis_cert;
  BigRat ball_multiplier = BigRat(0);
};

struct StoredMixedCert {
  int mix_k = 0;
  BigRat mix_weights[4] = {BigRat(0), BigRat(0), BigRat(0), BigRat(0)};
  StoredMixedComponent mix_components[4];
};

enum class NodeTag : uint8_t {
  NONE = 0,
  SPLIT,
  SPLIT_ORIGIN,
  SPLIT_VIEW,
  CERT,
  MX,
  TUBE,
  PRUNE_RADIUS,
  PRUNE_FUNDAMENTAL,
  DIFFICULT
};

struct ParsedNode {
  int64_t id = -1;
  int64_t parent_id = -1;
  int64_t child_ids[4] = {-1, -1, -1, -1};
  int depth = 0;
  int view_depth = 0;
  int shared_index = 0;
  int interval_id = -1;
  int triangle_id = -1;
  NodeTag tag = NodeTag::NONE;
  uint8_t fund_dir = 1;
  int winning_triple = -1;
  int inner[3] = {0, 0, 0};
  double tube_radius = 0.0;
  std::string tube_radius_str;
};

static BigRat ParseDecimalRat(std::string s) {
  while (!s.empty() && std::isspace(s.front())) s.erase(s.begin());
  while (!s.empty() && std::isspace(s.back())) s.pop_back();
  if (s.empty()) return BigRat(0);

  size_t e_pos = s.find_first_of("eE");
  if (e_pos != std::string::npos) {
    std::string base = s.substr(0, e_pos);
    int exp = std::stoi(s.substr(e_pos + 1));
    BigRat r = ParseDecimalRat(base);
    if (exp > 0) {
      for (int i = 0; i < exp; i++) r = r * BigRat(10);
    } else if (exp < 0) {
      for (int i = 0; i < -exp; i++) r = r / BigRat(10);
    }
    return r;
  }

  size_t slash_pos = s.find('/');
  if (slash_pos != std::string::npos) {
    return BigRat(s);
  }

  size_t dot_pos = s.find('.');
  if (dot_pos == std::string::npos) {
    return BigRat(s);
  }

  std::string int_part = s.substr(0, dot_pos);
  std::string frac_part = s.substr(dot_pos + 1);
  if (int_part.empty()) int_part = "0";

  bool neg = (!int_part.empty() && int_part[0] == '-');
  if (neg) int_part = int_part.substr(1);

  while (!frac_part.empty() && frac_part.back() == '0') frac_part.pop_back();
  if (frac_part.empty()) {
    BigInt val(int_part);
    if (neg) val = BigInt::Negate(std::move(val));
    return BigRat(val);
  }

  std::string full_digits = int_part + frac_part;
  size_t non_zero = full_digits.find_first_not_of('0');
  if (non_zero == std::string::npos) return BigRat(0);
  full_digits = full_digits.substr(non_zero);
  if (neg) full_digits = "-" + full_digits;

  BigInt num(full_digits);
  BigInt den = BigInt::Pow(BigInt(10), frac_part.size());
  return BigRat(num, den);
}

static bool ComputeExactComponent(const TriangleQ &tri,
                                   const CayleyBoxQ &box,
                                   int winning_triple,
                                   const int inner_in[3],
                                   int inner_out[3],
                                   std::shared_ptr<const TrianglePool> pool,
                                   AxisCertificateQ *cert_out,
                                   BigRat *ball_multiplier_out,
                                   int64_t node_id,
                                   bool verbose = true,
                                   bool check_displacement = true) {
  if (winning_triple < 0 || winning_triple >= (int)pool->gpu_triples.size()) {
    if (verbose) {
      std::cerr << "Error: node " << node_id << " winning triple " << winning_triple
                << " out of bounds (" << pool->gpu_triples.size() << " triples in pool).\n";
    }
    return false;
  }
  const auto &triple = pool->gpu_triples[winning_triple];
  const ContactInfo *contacts[3] = {
    &pool->contacts[triple.c0],
    &pool->contacts[triple.c1],
    &pool->contacts[triple.c2]
  };
  int inner[3] = {inner_in[0], inner_in[1], inner_in[2]};

  Vec3Q edges[3];
  for (int m = 0; m < 3; m++) {
    int start = contacts[m]->edge_start;
    int finish = contacts[m]->edge_finish;
    int start2 = contacts[m]->edge_start2;
    int finish2 = contacts[m]->edge_finish2;
    int mix = contacts[m]->mix;

    Vec3Q v_start = {VERTICES_Q[start][0], VERTICES_Q[start][1], VERTICES_Q[start][2]};
    Vec3Q v_finish = {VERTICES_Q[finish][0], VERTICES_Q[finish][1], VERTICES_Q[finish][2]};
    Vec3Q e1 = v_start - v_finish;

    Vec3Q v_start2 = {VERTICES_Q[start2][0], VERTICES_Q[start2][1], VERTICES_Q[start2][2]};
    Vec3Q v_finish2 = {VERTICES_Q[finish2][0], VERTICES_Q[finish2][1], VERTICES_Q[finish2][2]};
    Vec3Q e2 = v_start2 - v_finish2;

    BigRat lam(mix, 1000);
    edges[m] = e1 * lam + e2 * (BigRat(1) - lam);
  }

  Vec3Q coeff0 = Vec3Q::Cross(edges[1], edges[2]);
  Vec3Q coeff1 = Vec3Q::Cross(edges[2], edges[0]);
  Vec3Q coeff2 = Vec3Q::Cross(edges[0], edges[1]);

  BigRat p0 = Vec3Q::Dot(tri.corners[0], coeff0);
  BigRat p1 = Vec3Q::Dot(tri.corners[0], coeff1);
  BigRat p2 = Vec3Q::Dot(tri.corners[0], coeff2);

  if (p0 < 0 && p1 < 0 && p2 < 0) {
    std::swap(contacts[1], contacts[2]);
    std::swap(edges[1], edges[2]);
    std::swap(inner[1], inner[2]);
    coeff0 = Vec3Q::Cross(edges[1], edges[2]);
    coeff1 = Vec3Q::Cross(edges[2], edges[0]);
    coeff2 = Vec3Q::Cross(edges[0], edges[1]);
  }

  inner_out[0] = inner[0];
  inner_out[1] = inner[1];
  inner_out[2] = inner[2];

  Vec3Q w_coeffs[3] = {coeff0, coeff1, coeff2};
  BigRat support_error = BigRat(6, 1000000000000000LL); // 6 / 10^15
  BigRat sum_weight_upper(0);

  for (int m = 0; m < 3; m++) {
    BigRat w_max = Vec3Q::Dot(tri.corners[0], w_coeffs[m]);
    BigRat w_min = w_max;
    for (int c = 1; c < 3; c++) {
      BigRat val = Vec3Q::Dot(tri.corners[c], w_coeffs[m]);
      if (val > w_max) w_max = val;
      if (val < w_min) w_min = val;
    }
    if (w_min < support_error) {
      if (verbose) {
        std::cerr << "Warning: node " << node_id << " m=" << m << " w_min ("
                  << w_min.ToString() << ") < support_error ("
                  << support_error.ToString() << ")\n";
      }
      return false;
    }
    sum_weight_upper = sum_weight_upper + w_max + support_error;
  }

  if (check_displacement) {
    vec3d vin0 = {VERTICES_D[inner_out[0]][0], VERTICES_D[inner_out[0]][1], VERTICES_D[inner_out[0]][2]};
    vec3d vout0 = {VERTICES_D[contacts[0]->vertex][0], VERTICES_D[contacts[0]->vertex][1], VERTICES_D[contacts[0]->vertex][2]};
    vec3d vin1 = {VERTICES_D[inner_out[1]][0], VERTICES_D[inner_out[1]][1], VERTICES_D[inner_out[1]][2]};
    vec3d vout1 = {VERTICES_D[contacts[1]->vertex][0], VERTICES_D[contacts[1]->vertex][1], VERTICES_D[contacts[1]->vertex][2]};
    vec3d vin2 = {VERTICES_D[inner_out[2]][0], VERTICES_D[inner_out[2]][1], VERTICES_D[inner_out[2]][2]};
    vec3d vout2 = {VERTICES_D[contacts[2]->vertex][0], VERTICES_D[contacts[2]->vertex][1], VERTICES_D[contacts[2]->vertex][2]};

    vec3d e0 = contacts[0]->edge;
    vec3d e1 = contacts[1]->edge;
    vec3d e2 = contacts[2]->edge;
    vec3d wc0 = cross(e1, e2);
    vec3d wc1 = cross(e2, e0);
    vec3d wc2 = cross(e0, e1);

    vec3d tri_pts[6] = {
      tri.corners[0].ToVec3D(),
      tri.corners[1].ToVec3D(),
      tri.corners[2].ToVec3D(),
      (tri.corners[0].ToVec3D() + tri.corners[1].ToVec3D()) * 0.5,
      (tri.corners[1].ToVec3D() + tri.corners[2].ToVec3D()) * 0.5,
      (tri.corners[2].ToVec3D() + tri.corners[0].ToVec3D()) * 0.5
    };

    double min_x = box.center.x.ToDouble() - box.radii.x.ToDouble();
    double min_y = box.center.y.ToDouble() - box.radii.y.ToDouble();
    double min_z = box.center.z.ToDouble() - box.radii.z.ToDouble();
    double wx = 2.0 * box.radii.x.ToDouble();
    double wy = 2.0 * box.radii.y.ToDouble();
    double wz = 2.0 * box.radii.z.ToDouble();

    vec3d s_sgn = {1.0, 1.0, 1.0};
    double defect_penalty = triple.weighted_defect_upper * 1.0001;
    double disp_error = 1.8e-13;

    double min_all = 1e30;
    for (int pt_idx = 0; pt_idx < 6; pt_idx++) {
      double C[10] = {0};
      double w0 = dot(tri_pts[pt_idx], wc0);
      double w1 = dot(tri_pts[pt_idx], wc1);
      double w2 = dot(tri_pts[pt_idx], wc2);
      AccumulateContactPoly(C, w0, cross(tri_pts[pt_idx], e0), vin0, vout0, s_sgn);
      AccumulateContactPoly(C, w1, cross(tri_pts[pt_idx], e1), vin1, vout1, s_sgn);
      AccumulateContactPoly(C, w2, cross(tri_pts[pt_idx], e2), vin2, vout2, s_sgn);
      double m = Bernstein27Min(C, min_x, min_y, min_z, wx, wy, wz);
      if (m < min_all) min_all = m;
    }
    double margin = min_all - defect_penalty - disp_error;
    if (margin <= 0.0) {
      if (verbose) {
        std::cerr << "Warning: node " << node_id << " displacement margin <= 0 (" << margin << ")\n";
      }
      return false;
    }
  }


  BigRat exact_B = sum_weight_upper * BigRat(2);
  BigRat denom_B(1000000000LL);
  BigInt scaled_B = (exact_B * denom_B).Numerator();
  BigInt div_B = (exact_B * denom_B).Denominator();
  BigInt ceil_val = (scaled_B + div_B - 1) / div_B;
  BigRat B = BigRat(ceil_val, denom_B.Numerator());

  cert_out->B = B;

  for (int m = 0; m < 3; m++) {
    cert_out->edge_start[m] = contacts[m]->edge_start;
    cert_out->edge_finish[m] = contacts[m]->edge_finish;
    cert_out->edge_start2[m] = contacts[m]->edge_start2;
    cert_out->edge_finish2[m] = contacts[m]->edge_finish2;
    cert_out->mix[m] = contacts[m]->mix;
    cert_out->support_index[m] = contacts[m]->vertex;

    int sel = contacts[m]->vertex;
    Vec3Q v_sel = {VERTICES_Q[sel][0], VERTICES_Q[sel][1], VERTICES_Q[sel][2]};
    BigRat best_support = BigRat(1000000);
    int best_w = -1;

    for (int k = 0; k < 20; k++) {
      bool tie = (k == sel) ||
                 (contacts[m]->mix == 1000 && sel == contacts[m]->edge_finish && k == contacts[m]->edge_start) ||
                 (contacts[m]->mix == 0 && sel == contacts[m]->edge_start2 && k == contacts[m]->edge_finish2);
      if (tie) continue;
      Vec3Q v_k = {VERTICES_Q[k][0], VERTICES_Q[k][1], VERTICES_Q[k][2]};
      Vec3Q delta = v_k - v_sel;
      Vec3Q s_coeff = Vec3Q::Cross(edges[m], delta);
      BigRat max_s = Vec3Q::Dot(tri.corners[0], s_coeff);
      for (int c = 1; c < 3; c++) {
        BigRat val = Vec3Q::Dot(tri.corners[c], s_coeff);
        if (val > max_s) max_s = val;
      }
      BigRat s_upper = max_s + support_error;
      if (s_upper < best_support) {
        best_support = s_upper;
        best_w = k;
      }
    }
    cert_out->nonzero_witness[m] = best_w;
    if (best_support >= 0) {
      if (verbose) {
        std::cerr << "Warning: node " << node_id << " contact " << m
                  << " best_support >= 0: " << best_support.ToString() << "\n";
      }
      return false;
    }
  }

  *ball_multiplier_out = BigRat(0);
  return true;
}

static bool ParseRow(const std::string &line,
                    ParsedNode *node,
                    std::unordered_map<int64_t, StoredMixedCert> *mixed_certs,
                    int64_t *cert_delta) {
  if (line.empty()) return false;
  std::istringstream iss(line);
  std::string tag_str;
  int64_t id, parent_id;
  int depth;
  if (!(iss >> tag_str >> id >> parent_id >> depth)) return false;

  node->id = id;
  node->parent_id = parent_id;
  node->depth = depth;

  if (tag_str == "CE" || tag_str == "CERT") {
    node->tag = NodeTag::CERT;
    if (cert_delta) (*cert_delta)++;
    double margin = 0.0;
    iss >> node->winning_triple >> margin >> node->inner[0] >> node->inner[1] >> node->inner[2];
  } else if (tag_str == "MX") {
    node->tag = NodeTag::MX;
    if (cert_delta) (*cert_delta)++;
    int k = 0;
    double margin = 0.0;
    if (iss >> k >> margin) {
      StoredMixedCert mx;
      mx.mix_k = k;
      std::vector<double> float_weights(k);
      for (int i = 0; i < k; i++) {
        iss >> mx.mix_components[i].winning_triple
            >> float_weights[i]
            >> mx.mix_components[i].inner[0]
            >> mx.mix_components[i].inner[1]
            >> mx.mix_components[i].inner[2];
      }
      int64_t M = 10000000LL;
      int64_t sum_w = 0;
      for (int i = 0; i < k - 1; i++) {
        int64_t W = std::max<int64_t>(0LL, (int64_t)std::round(float_weights[i] * M));
        mx.mix_weights[i] = BigRat(W, M);
        sum_w += W;
      }
      int64_t rem = M - sum_w;
      if (rem < 0) rem = 0;
      mx.mix_weights[k - 1] = BigRat(rem, M);
      for (int i = k; i < 4; i++) {
        mx.mix_components[i] = mx.mix_components[0];
        mx.mix_weights[i] = BigRat(0);
      }
      if (mixed_certs) {
        (*mixed_certs)[id] = std::move(mx);
      }
    }
  } else if (tag_str == "PR" || tag_str == "PRUNE") {
    std::string prune_kind;
    iss >> prune_kind;
    if (prune_kind == "RADIUS" || prune_kind == "RA") {
      node->tag = NodeTag::PRUNE_RADIUS;
    } else if (prune_kind == "FUNDAMENTAL" || prune_kind == "FU") {
      node->tag = NodeTag::PRUNE_FUNDAMENTAL;
      int fdir = 1;
      iss >> fdir;
      node->fund_dir = fdir;
    }
  } else if (tag_str == "TU" || tag_str == "TUBE") {
    node->tag = NodeTag::TUBE;
    if (iss >> node->tube_radius_str) {
      node->tube_radius = std::stod(node->tube_radius_str);
    }
  } else if (tag_str == "SP" || tag_str == "SPLIT") {
    node->tag = NodeTag::SPLIT;
    int64_t c0, c1;
    if (iss >> c0 >> c1) {
      node->child_ids[0] = c0;
      node->child_ids[1] = c1;
    }
  } else if (tag_str == "SO" || tag_str == "SPLIT_ORIGIN") {
    node->tag = NodeTag::SPLIT_ORIGIN;
    int64_t c0, c1;
    if (iss >> c0 >> c1) {
      node->child_ids[0] = c0;
      node->child_ids[1] = c1;
    }
  } else if (tag_str == "SV" || tag_str == "SPLIT_VIEW") {
    node->tag = NodeTag::SPLIT_VIEW;
    int64_t c0, c1, c2, c3;
    if (iss >> c0 >> c1 >> c2 >> c3) {
      node->child_ids[0] = c0;
      node->child_ids[1] = c1;
      node->child_ids[2] = c2;
      node->child_ids[3] = c3;
    }
  } else if (tag_str == "DF" || tag_str == "DI" || tag_str == "DIFFICULT") {
    node->tag = NodeTag::DIFFICULT;
  } else {
    node->tag = NodeTag::NONE;
  }
  return true;
}

static void LoadDoneFiles(const std::string &done_dir,
                          int chart,
                          std::vector<ParsedNode> *nodes,
                          std::unordered_map<int64_t, StoredMixedCert> *mixed_certs,
                          std::vector<int64_t> *all_node_ids,
                          int64_t *max_id_io,
                          int64_t *cert_count_io) {
  if (done_dir.empty() || done_dir == "none") return;
  std::filesystem::path dir_path(done_dir);
  if (!std::filesystem::exists(dir_path) || !std::filesystem::is_directory(dir_path)) {
    return;
  }

  std::string prefix = "chart" + std::to_string(chart) + ".";
  std::string suffix = ".done";

  std::vector<std::string> matching_files;
  for (const auto &entry : std::filesystem::directory_iterator(dir_path)) {
    if (!entry.is_regular_file()) continue;
    std::string fname = entry.path().filename().string();
    if (fname.rfind(prefix, 0) == 0 && fname.size() >= suffix.size() &&
        fname.compare(fname.size() - suffix.size(), suffix.size(), suffix) == 0) {
      matching_files.push_back(entry.path().string());
    }
  }
  std::sort(matching_files.begin(), matching_files.end());

  int done_files_found = 0;
  int done_files_merged = 0;
  int64_t total_spliced_nodes = 0;
  int64_t next_avail_id = *max_id_io + 1;

  for (const auto &fpath : matching_files) {
    done_files_found++;
    std::ifstream f(fpath);
    if (!f.is_open()) continue;

    std::vector<ParsedNode> file_rows;
    std::unordered_map<int64_t, StoredMixedCert> file_mx;
    std::string line;
    int64_t dummy_certs = 0;
    while (std::getline(f, line)) {
      if (line.empty()) continue;
      ParsedNode pn;
      if (ParseRow(line, &pn, &file_mx, &dummy_certs)) {
        file_rows.push_back(pn);
      }
    }
    if (file_rows.empty()) continue;

    int64_t file_root_id = file_rows[0].id;
    if (file_root_id < 0 || file_root_id >= (int64_t)nodes->size() ||
        (*nodes)[file_root_id].tag == NodeTag::NONE) {
      continue;
    }

    std::unordered_map<int64_t, int64_t> id_map;
    id_map[file_root_id] = file_root_id;
    for (size_t i = 1; i < file_rows.size(); i++) {
      id_map[file_rows[i].id] = next_avail_id++;
    }

    if ((*nodes)[file_root_id].tag == NodeTag::CERT || (*nodes)[file_root_id].tag == NodeTag::MX) {
      (*cert_count_io)--;
    }

    auto &root_node = (*nodes)[file_root_id];
    root_node.tag = file_rows[0].tag;
    root_node.winning_triple = file_rows[0].winning_triple;
    for (int m = 0; m < 3; m++) root_node.inner[m] = file_rows[0].inner[m];
    root_node.tube_radius = file_rows[0].tube_radius;
    root_node.tube_radius_str = file_rows[0].tube_radius_str;
    root_node.fund_dir = file_rows[0].fund_dir;
    for (int m = 0; m < 4; m++) {
      if (file_rows[0].child_ids[m] >= 0 && id_map.count(file_rows[0].child_ids[m])) {
        root_node.child_ids[m] = id_map[file_rows[0].child_ids[m]];
      } else {
        root_node.child_ids[m] = -1;
      }
    }
    if (root_node.tag == NodeTag::MX) {
      (*mixed_certs)[file_root_id] = std::move(file_mx[file_root_id]);
    }
    if (root_node.tag == NodeTag::CERT || root_node.tag == NodeTag::MX) {
      (*cert_count_io)++;
    }

    if (next_avail_id >= (int64_t)nodes->size()) {
      nodes->resize(std::max((int64_t)nodes->size() * 2, next_avail_id + 1024));
    }

    for (size_t i = 1; i < file_rows.size(); i++) {
      ParsedNode row = file_rows[i];
      int64_t new_id = id_map[row.id];
      int64_t new_parent_id = id_map[row.parent_id];
      row.id = new_id;
      row.parent_id = new_parent_id;
      for (int m = 0; m < 4; m++) {
        if (row.child_ids[m] >= 0 && id_map.count(row.child_ids[m])) {
          row.child_ids[m] = id_map[row.child_ids[m]];
        } else {
          row.child_ids[m] = -1;
        }
      }
      if (row.tag == NodeTag::MX) {
        (*mixed_certs)[new_id] = std::move(file_mx[file_rows[i].id]);
      }
      if (row.tag == NodeTag::CERT || row.tag == NodeTag::MX) {
        (*cert_count_io)++;
      }
      (*nodes)[new_id] = row;
      all_node_ids->push_back(new_id);
      total_spliced_nodes++;
    }

    done_files_merged++;
  }

  *max_id_io = next_avail_id - 1;

  if (done_files_merged > 0) {
    std::cout << "Merged " << done_files_merged << " / " << done_files_found
              << " .done files (" << total_spliced_nodes << " new descendant nodes spliced, total nodes now "
              << all_node_ids->size() << ").\n";
  }
}

int main(int argc, char **argv) {
  int chart = 0;
  std::string in_path;
  std::string out_path;
  int cone_samples = 8;
  int escalate_depth = 36;
  int escalate_cone_samples = 12;
  int deep_escalate_depth = 38;
  int deep_escalate_cone_samples = 14;
  bool fill_pending = false;
  bool legacy_sort = false;
  std::string done_dir = ".";

  int pos_arg = 0;
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--cone_samples" && i + 1 < argc) {
      cone_samples = std::atoi(argv[++i]);
    } else if (arg == "--escalate_depth" && i + 1 < argc) {
      escalate_depth = std::atoi(argv[++i]);
    } else if (arg == "--escalate_cone_samples" && i + 1 < argc) {
      escalate_cone_samples = std::atoi(argv[++i]);
    } else if (arg == "--deep_escalate_depth" && i + 1 < argc) {
      deep_escalate_depth = std::atoi(argv[++i]);
    } else if (arg == "--deep_escalate_cone_samples" && i + 1 < argc) {
      deep_escalate_cone_samples = std::atoi(argv[++i]);
    } else if (arg == "--fill_pending") {
      fill_pending = true;
    } else if (arg == "--legacy_sort") {
      legacy_sort = true;
    } else if (arg == "--done_dir" && i + 1 < argc) {
      done_dir = argv[++i];
    } else if (arg == "--no_done") {
      done_dir = "none";
    } else if (!arg.empty() && arg[0] != '-') {
      if (pos_arg == 0) chart = std::atoi(arg.c_str());
      else if (pos_arg == 1) in_path = arg;
      else if (pos_arg == 2) out_path = arg;
      pos_arg++;
    }
  }

  if (in_path.empty()) in_path = "chart" + std::to_string(chart) + ".rows.log";
  if (out_path.empty()) out_path = "chart" + std::to_string(chart) + ".pack";

  std::cout << "Packing chart " << chart << " from " << in_path << " into " << out_path << "...\n";
  Timer total_timer;

  // Chart setup
  CayleyBoxQ root_box;
  root_box.center = Vec3Q(BigRat(0), BigRat(0), BigRat(0));
  if (chart == 0) {
    root_box.radii = Vec3Q(BigRat(1), BigRat(1), BigRat(1, 3));
  } else if (chart == 1) {
    root_box.radii = Vec3Q(BigRat(1), BigRat(1, 3), BigRat(1));
  } else if (chart == 2) {
    root_box.radii = Vec3Q(BigRat(1, 3), BigRat(1), BigRat(1));
  } else {
    root_box.radii = Vec3Q(BigRat(1), BigRat(1), BigRat(1));
  }

  TriangleQ root_tri;
  root_tri.corners[0] = Vec3Q(BigRat(1), BigRat(0), BigRat(0));
  root_tri.corners[1] = Vec3Q(BigRat(10, 41), BigRat(31, 41), BigRat(0));
  root_tri.corners[2] = Vec3Q(BigRat(0), BigRat(0), BigRat(1));

  std::vector<ParsedNode> nodes(100000);
  std::unordered_map<int64_t, StoredMixedCert> mixed_certs;
  std::vector<int64_t> all_node_ids;
  int64_t max_id = -1;
  int64_t cert_count = 0;

  std::ifstream infile(in_path);
  if (!infile.is_open()) {
    std::cerr << "Error: cannot open " << in_path << "\n";
    return 1;
  }

  std::cout << "Reading " << in_path << "..." << std::flush;
  Timer read_timer;
  std::string line;
  while (std::getline(infile, line)) {
    if (line.empty()) continue;
    ParsedNode pn;
    int64_t cert_delta = 0;
    if (!ParseRow(line, &pn, &mixed_certs, &cert_delta)) continue;
    if (pn.id >= (int64_t)nodes.size()) {
      nodes.resize(std::max((int64_t)nodes.size() * 2, pn.id + 1024));
    }
    nodes[pn.id] = pn;
    all_node_ids.push_back(pn.id);
    if (pn.id > max_id) max_id = pn.id;
    cert_count += cert_delta;
  }
  infile.close();

  LoadDoneFiles(done_dir, chart, &nodes, &mixed_certs, &all_node_ids, &max_id, &cert_count);

  std::cout << " done (" << all_node_ids.size() << " nodes, "
            << cert_count << " certificates) in " << read_timer.Seconds() << "s.\n";

  // Deduplication structures
  std::vector<CayleyBoxQ> intervals;
  std::unordered_map<std::string, int> interval_index_map;
  auto get_box_key = [](const CayleyBoxQ &b) -> std::string {
    return b.center.x.ToString() + "_" + b.center.y.ToString() + "_" + b.center.z.ToString() + "|" +
           b.radii.x.ToString() + "_" + b.radii.y.ToString() + "_" + b.radii.z.ToString();
  };
  auto get_interval_id = [&](const CayleyBoxQ &b) -> int {
    std::string key = get_box_key(b);
    auto it = interval_index_map.find(key);
    if (it != interval_index_map.end()) return it->second;
    int idx = intervals.size();
    intervals.push_back(b);
    interval_index_map.emplace(key, idx);
    return idx;
  };

  std::vector<TriangleQ> triangles;
  std::unordered_map<std::string, int> triangle_index_map;
  auto get_tri_key = [](const TriangleQ &tri) -> std::string {
    return tri.corners[0].x.ToString() + "_" + tri.corners[0].y.ToString() + "_" + tri.corners[0].z.ToString() + "|" +
           tri.corners[1].x.ToString() + "_" + tri.corners[1].y.ToString() + "_" + tri.corners[1].z.ToString() + "|" +
           tri.corners[2].x.ToString() + "_" + tri.corners[2].y.ToString() + "_" + tri.corners[2].z.ToString();
  };
  auto get_triangle_id = [&](const TriangleQ &tri) -> int {
    std::string key = get_tri_key(tri);
    auto it = triangle_index_map.find(key);
    if (it != triangle_index_map.end()) return it->second;
    int idx = triangles.size();
    triangles.push_back(tri);
    triangle_index_map.emplace(key, idx);
    return idx;
  };

  int root_interval_idx = get_interval_id(root_box);
  int root_triangle_idx = get_triangle_id(root_tri);
  nodes[0].interval_id = root_interval_idx;
  nodes[0].triangle_id = root_triangle_idx;

  auto root_sub_wedges = root_tri.Subdivide();
  int root_sub_wedge_ids[4] = {
    get_triangle_id(root_sub_wedges[0]),
    get_triangle_id(root_sub_wedges[1]),
    get_triangle_id(root_sub_wedges[2]),
    get_triangle_id(root_sub_wedges[3])
  };

  // Reconstruct search tree via BFS
  std::cout << "Reconstructing search tree..." << std::flush;
  Timer bfs_timer;
  std::vector<int64_t> bfs_queue;
  int64_t root_c[4] = {
    nodes[0].child_ids[0],
    nodes[0].child_ids[1],
    nodes[0].child_ids[2],
    nodes[0].child_ids[3]
  };

  for (int t = 0; t < 4; t++) {
    int64_t cid = root_c[t];
    if (cid >= 0 && cid < (int64_t)nodes.size() && nodes[cid].tag != NodeTag::NONE) {
      nodes[cid].interval_id = root_interval_idx;
      nodes[cid].triangle_id = root_sub_wedge_ids[t];
      nodes[cid].shared_index = t;
      bfs_queue.push_back(cid);
    }
  }

  size_t bfs_head = 0;
  while (bfs_head < bfs_queue.size()) {
    int64_t curr_id = bfs_queue[bfs_head++];
    const auto &curr = nodes[curr_id];

    if (curr.tag == NodeTag::SPLIT || curr.tag == NodeTag::SPLIT_ORIGIN) {
      int64_t c0 = curr.child_ids[0];
      int64_t c1 = curr.child_ids[1];
      const CayleyBoxQ &p_box = intervals[curr.interval_id];
      int axis = p_box.WidestAxis();

      CayleyBoxQ b0 = p_box;
      CayleyBoxQ b1 = p_box;
      if (axis == 0) {
        b0.radii.x = b0.radii.x / BigRat(2);
        b0.center.x = b0.center.x - b0.radii.x;
        b1.radii.x = b1.radii.x / BigRat(2);
        b1.center.x = b1.center.x + b1.radii.x;
      } else if (axis == 1) {
        b0.radii.y = b0.radii.y / BigRat(2);
        b0.center.y = b0.center.y - b0.radii.y;
        b1.radii.y = b1.radii.y / BigRat(2);
        b1.center.y = b1.center.y + b1.radii.y;
      } else {
        b0.radii.z = b0.radii.z / BigRat(2);
        b0.center.z = b0.center.z - b0.radii.z;
        b1.radii.z = b1.radii.z / BigRat(2);
        b1.center.z = b1.center.z + b1.radii.z;
      }

      int iv0 = get_interval_id(b0);
      int iv1 = get_interval_id(b1);

      if (c0 >= 0 && c0 < (int64_t)nodes.size() && nodes[c0].tag != NodeTag::NONE) {
        nodes[c0].interval_id = iv0;
        nodes[c0].triangle_id = curr.triangle_id;
        nodes[c0].shared_index = curr.shared_index;
        bfs_queue.push_back(c0);
      }
      if (c1 >= 0 && c1 < (int64_t)nodes.size() && nodes[c1].tag != NodeTag::NONE) {
        nodes[c1].interval_id = iv1;
        nodes[c1].triangle_id = curr.triangle_id;
        nodes[c1].shared_index = curr.shared_index;
        bfs_queue.push_back(c1);
      }

    } else if (curr.tag == NodeTag::SPLIT_VIEW) {
      const TriangleQ &p_tri = triangles[curr.triangle_id];
      auto sub_tris = p_tri.Subdivide();
      int sub_tri_ids[4] = {
        get_triangle_id(sub_tris[0]),
        get_triangle_id(sub_tris[1]),
        get_triangle_id(sub_tris[2]),
        get_triangle_id(sub_tris[3])
      };
      for (int t = 0; t < 4; t++) {
        int64_t cid = curr.child_ids[t];
        if (cid >= 0 && cid < (int64_t)nodes.size() && nodes[cid].tag != NodeTag::NONE) {
          nodes[cid].interval_id = curr.interval_id;
          nodes[cid].triangle_id = sub_tri_ids[t];
          nodes[cid].shared_index = curr.shared_index;
          bfs_queue.push_back(cid);
        }
      }
    }
  }
  std::cout << " done (" << intervals.size() << " intervals, "
            << triangles.size() << " triangles) in " << bfs_timer.Seconds() << "s.\n";

  // Group certificate nodes by triangle to guarantee minimal RAM usage
  std::vector<std::vector<int64_t>> certs_by_tri(triangles.size());
  for (int64_t id : all_node_ids) {
    if (nodes[id].tag == NodeTag::CERT || nodes[id].tag == NodeTag::MX) {
      certs_by_tri[nodes[id].triangle_id].push_back(id);
    }
  }

  std::cout << "Computing exact rational certificates...\n";
  Timer cert_timer;
  std::vector<StoredCert> stored_certs(max_id + 1);

  int64_t certs_done = 0;
  for (size_t t = 0; t < triangles.size(); t++) {
    if (certs_by_tri[t].empty()) continue;
    const TriangleQ &tri = triangles[t];

    vec3d tri_d[3];
    for (int c = 0; c < 3; c++) {
      tri_d[c].x = tri.corners[c].x.ToDouble();
      tri_d[c].y = tri.corners[c].y.ToDouble();
      tri_d[c].z = tri.corners[c].z.ToDouble();
    }

    std::map<int, std::shared_ptr<const TrianglePool>> tri_pools;
    auto get_pool = [&](int samples) -> std::shared_ptr<const TrianglePool> {
      auto it = tri_pools.find(samples);
      if (it != tri_pools.end()) return it->second;
      auto pool = BuildTrianglePool(tri_d, samples, legacy_sort);
      tri_pools.emplace(samples, pool);
      return pool;
    };

    auto get_pool_for_triple = [&](int depth, int max_triple) -> std::shared_ptr<const TrianglePool> {
      int samples = cone_samples;
      if (deep_escalate_depth > 0 && depth >= deep_escalate_depth) {
        samples = deep_escalate_cone_samples;
      } else if (escalate_depth > 0 && depth >= escalate_depth) {
        samples = escalate_cone_samples;
      }
      auto pool = get_pool(samples);
      if (max_triple < (int)pool->gpu_triples.size()) return pool;
      static const int escalate_steps[] = {8, 10, 12, 14, 16, 18, 20};
      for (int s : escalate_steps) {
        if (s <= samples) continue;
        pool = get_pool(s);
        if (max_triple < (int)pool->gpu_triples.size()) return pool;
      }
      return pool;
    };

    for (int64_t id : certs_by_tri[t]) {
      const auto &node = nodes[id];
      if (node.tag == NodeTag::CERT) {
        int primary_samples = cone_samples;
        if (deep_escalate_depth > 0 && node.depth >= deep_escalate_depth) {
          primary_samples = deep_escalate_cone_samples;
        } else if (escalate_depth > 0 && node.depth >= escalate_depth) {
          primary_samples = escalate_cone_samples;
        }

        std::vector<int> candidate_samples = {primary_samples};
        for (int s : {12, 8, 14, 16, 10, 18, 20}) {
          if (s != primary_samples) candidate_samples.push_back(s);
        }

        StoredCert sc;
        bool verified = false;
        const CayleyBoxQ &box = intervals[node.interval_id];
        for (int s : candidate_samples) {
          auto pool = get_pool(s);
          if (node.winning_triple >= (int)pool->gpu_triples.size()) continue;
          if (ComputeExactComponent(tri, box, node.winning_triple, node.inner, sc.inner, pool,
                                     &sc.axis_cert, &sc.ball_multiplier, id, false, true)) {
            verified = true;
            break;
          }
        }

        if (!verified) {
          for (int s : candidate_samples) {
            auto pool = get_pool(s);
            if (node.winning_triple < (int)pool->gpu_triples.size()) {
              std::cerr << "Attempting with samples=" << s << ":\n";
              ComputeExactComponent(tri, box, node.winning_triple, node.inner, sc.inner, pool,
                                    &sc.axis_cert, &sc.ball_multiplier, id, true, true);
            }
          }
          std::cerr << "\n" << ARED("Failed to verify certificate for node ") << id << "\n";
          return 1;
        }
        stored_certs[id] = sc;
      } else if (node.tag == NodeTag::MX) {
        auto &mx = mixed_certs[id];
        int max_triple = 0;
        for (int i = 0; i < mx.mix_k; i++) {
          if (mx.mix_components[i].winning_triple > max_triple) {
            max_triple = mx.mix_components[i].winning_triple;
          }
        }
        auto pool = get_pool_for_triple(node.depth, max_triple);
        const CayleyBoxQ &box = intervals[node.interval_id];
        for (int i = 0; i < mx.mix_k; i++) {
          if (!ComputeExactComponent(tri,
                                     box,
                                     mx.mix_components[i].winning_triple,
                                     mx.mix_components[i].inner,
                                     mx.mix_components[i].inner,
                                     pool,
                                     &mx.mix_components[i].axis_cert,
                                     &mx.mix_components[i].ball_multiplier,
                                     id,
                                     true,
                                     false)) {
            std::cerr << "\n" << ARED("Failed to verify mixed component ") << i << " for node " << id << "\n";
            return 1;
          }
        }
        for (int i = mx.mix_k; i < 4; i++) {
          mx.mix_components[i] = mx.mix_components[0];
          mx.mix_weights[i] = BigRat(0);
        }
      }
      certs_done++;
      if (certs_done % 10000 == 0 || certs_done == cert_count) {
        std::cout << "\rVerified " << certs_done << " / " << cert_count << " certificates..." << std::flush;
      }
    }
  }
  std::cout << " done in " << cert_timer.Seconds() << "s.\n";

  // Write Lean packed format
  std::cout << "Writing packed file " << out_path << "..." << std::flush;
  std::ofstream outfile(out_path, std::ios::binary);
  if (!outfile.is_open()) {
    std::cerr << "Error: cannot open output file " << out_path << "\n";
    return 1;
  }

  int64_t total_lean_rows = max_id + 2;
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

  // Row 0: viewRoot (tag 0 in readRow)
  // tag 0: id intervalIndex child
  outfile << ",0,0," << root_interval_idx << ",1";

  // Row 1: viewSplit (tag 2 in readRow)
  // tag 2: id intervalIndex c0 c1 c2 c3 0 triangleIndex
  outfile << ",2,1," << root_interval_idx
          << "," << (root_c[0] + 1)
          << "," << (root_c[1] + 1)
          << "," << (root_c[2] + 1)
          << "," << (root_c[3] + 1)
          << ",0," << root_triangle_idx;

  // Rows 2 .. total_lean_rows - 1: search tree nodes 0 .. max_id
  for (int64_t lean_id = 2; lean_id < total_lean_rows; lean_id++) {
    int64_t id = lean_id - 1;
    if (id >= (int64_t)nodes.size() || nodes[id].tag == NodeTag::NONE || nodes[id].interval_id < 0) {
      if (fill_pending) {
        outfile << ",0," << lean_id << "," << root_interval_idx << "," << lean_id;
        continue;
      } else {
        std::cerr << "\nError: Missing or unreachable node " << id << " in tree.\n";
        return 1;
      }
    }
    const auto &node = nodes[id];
    int iv_idx = node.interval_id;
    int tri_idx = node.triangle_id;

    if (node.tag == NodeTag::SPLIT || node.tag == NodeTag::SPLIT_ORIGIN) {
      int64_t c0 = node.child_ids[0];
      int64_t c1 = node.child_ids[1];
      if (c0 < 0 || c1 < 0) {
        if (fill_pending) {
          outfile << ",0," << lean_id << "," << iv_idx << "," << lean_id;
          continue;
        } else {
          std::cerr << "\nError: Node " << id << " has missing children.\n";
          return 1;
        }
      }
      const CayleyBoxQ &box = intervals[iv_idx];
      int axis = box.WidestAxis();
      int coord = axis + 2; // In Lean: x=2, y=3, z=4
      outfile << ",1," << lean_id << "," << iv_idx
              << "," << (c0 + 1) << "," << (c1 + 1)
              << "," << coord << ",0," << tri_idx;

    } else if (node.tag == NodeTag::SPLIT_VIEW) {
      bool missing_child = false;
      for (int t = 0; t < 4; t++) {
        if (node.child_ids[t] < 0) {
          missing_child = true;
          break;
        }
      }
      if (missing_child) {
        if (fill_pending) {
          outfile << ",0," << lean_id << "," << iv_idx << "," << lean_id;
          continue;
        } else {
          std::cerr << "\nError: Node " << id << " has missing children.\n";
          return 1;
        }
      }
      outfile << ",2," << lean_id << "," << iv_idx
              << "," << (node.child_ids[0] + 1)
              << "," << (node.child_ids[1] + 1)
              << "," << (node.child_ids[2] + 1)
              << "," << (node.child_ids[3] + 1)
              << ",0," << tri_idx;

    } else if (node.tag == NodeTag::DIFFICULT) {
      if (fill_pending) {
        int safe_iv = (iv_idx >= 0) ? iv_idx : root_interval_idx;
        outfile << ",0," << lean_id << "," << safe_iv << "," << lean_id;
        continue;
      } else {
        std::cerr << "\nError: Difficult node " << id << " in tree without certificate.\n";
        return 1;
      }

    } else if (node.tag == NodeTag::CERT) {
      const auto &sc = stored_certs[id];
      const auto &c = sc.axis_cert;
      outfile << ",4," << lean_id << "," << iv_idx << ",0," << tri_idx;
      for (int m = 0; m < 3; m++) outfile << "," << c.edge_start[m];
      for (int m = 0; m < 3; m++) outfile << "," << c.edge_finish[m];
      for (int m = 0; m < 3; m++) outfile << "," << c.edge_start2[m];
      for (int m = 0; m < 3; m++) outfile << "," << c.edge_finish2[m];
      for (int m = 0; m < 3; m++) outfile << "," << c.mix[m];
      for (int m = 0; m < 3; m++) outfile << "," << c.support_index[m];
      for (int m = 0; m < 3; m++) outfile << "," << c.nonzero_witness[m];
      outfile << "," << ZigzagRatString(c.B);
      outfile << "," << sc.inner[0] << "," << sc.inner[1] << "," << sc.inner[2];
      outfile << "," << ZigzagRatString(sc.ball_multiplier);

    } else if (node.tag == NodeTag::MX) {
      const auto &mx = mixed_certs[id];
      outfile << ",9," << lean_id << "," << iv_idx << ",0," << tri_idx;
      for (int m = 0; m < 4; m++) {
        outfile << "," << ZigzagRatString(mx.mix_weights[m]);
      }
      for (int k = 0; k < 4; k++) {
        const auto &comp = mx.mix_components[k];
        const auto &c = comp.axis_cert;
        for (int m = 0; m < 3; m++) outfile << "," << c.edge_start[m];
        for (int m = 0; m < 3; m++) outfile << "," << c.edge_finish[m];
        for (int m = 0; m < 3; m++) outfile << "," << c.edge_start2[m];
        for (int m = 0; m < 3; m++) outfile << "," << c.edge_finish2[m];
        for (int m = 0; m < 3; m++) outfile << "," << c.mix[m];
        for (int m = 0; m < 3; m++) outfile << "," << c.support_index[m];
        for (int m = 0; m < 3; m++) outfile << "," << c.nonzero_witness[m];
        outfile << "," << ZigzagRatString(c.B);
        outfile << "," << comp.inner[0] << "," << comp.inner[1] << "," << comp.inner[2];
        outfile << "," << ZigzagRatString(comp.ball_multiplier);
      }

    } else if (node.tag == NodeTag::TUBE) {
      int tube_node_id = 0;
      BigRat r_rat = ParseDecimalRat(node.tube_radius_str.empty() ?
                                     std::to_string(node.tube_radius) :
                                     node.tube_radius_str);
      outfile << ",6," << lean_id << "," << iv_idx
              << ",0," << ZigzagRatString(r_rat)
              << "," << node.shared_index
              << "," << tube_node_id
              << ",0," << tri_idx;

    } else if (node.tag == NodeTag::PRUNE_RADIUS) {
      outfile << ",7," << lean_id << "," << iv_idx
              << ",0," << tri_idx;

    } else if (node.tag == NodeTag::PRUNE_FUNDAMENTAL) {
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
