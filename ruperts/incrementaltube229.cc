// Identity Tube Incremental BFS Search & Dual-Bound Engine for Nopert #229
// Implements canonical base-4 tree search using tubetree229 library.

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <deque>
#include <cmath>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <cassert>
#include <filesystem>
#include <format>
#include <array>
#include <algorithm>
#include <numeric>
#include <set>
#include <random>
#include <ctime>

#include "bignum/big.h"
#include "bignum/big-overloads.h"
#include "geom/hull-2d.h"
#include "yocto-math.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "ansi.h"
#include "util.h"
#include "numbers.h"

#include "tubetree229.h"

using vec2 = yocto::vec<double, 2>;
using vec3 = yocto::vec<double, 3>;
using namespace tubetree229;

// ============================================================================
// POLYHEDRON #229 GEOMETRY
// ============================================================================

static const vec3 VERTICES_D[20] = {
  {0.0428407320766475, 0.5680663556187131, 0.5648167326177671},
  {-0.1710940528198280, 0.9384169756351713, 0.3001672949030625},
  {-0.2791996671138589, 0.8916783831939151, -0.0420605170858861},
  {0.0581211699562287, 0.6025790913331870, -0.7643399516054460},
  {-0.5270246949360691, 0.2162861152231751, 0.5648167326177671},
  {-0.9453585496376318, 0.1272666794475643, 0.3001672949030625},
  {-0.9343139787381105, 0.0100091111676232, -0.0420605170858861},
  {-0.5551263421462106, 0.2414836970985378, -0.7643399516054460},
  {-0.3685599064576828, -0.4343941851161148, 0.5648167326177671},
  {-0.4131696624115331, -0.8597618421012388, 0.3001672949030625},
  {-0.2982381279104400, -0.8854924122951479, -0.0420605170858861},
  {-0.4012081174529903, -0.4533339587973062, -0.7643399516054460},
  {0.2992421458547392, -0.4847564861402479, 0.5648167326177671},
  {0.6900056551469845, -0.6586287200963504, 0.3001672949030625},
  {0.7499926789483198, -0.5572735187461599, -0.0420605170858861},
  {0.3071660889979028, -0.5216594918898176, -0.7643399516054460},
  {0.5535017234623651, 0.1347982004144742, 0.5648167326177671},
  {0.8396166097220085, 0.4527069071148534, 0.3001672949030625},
  {0.7617590948140895, 0.5410784366797694, -0.0420605170858861},
  {0.5910472006450693, 0.1309306622553988, -0.7643399516054460}
};

static const std::string_view VERTICES_Q[20][3] = {
  {"17136292830659/400000000000000", "5680663556187131/10000000000000000", "5648167326177671/10000000000000000"},
  {"-42773513204957/250000000000000", "9384169756351713/10000000000000000", "4802676718449/16000000000000"},
  {"-2791996671138589/10000000000000000", "8916783831939151/10000000000000000", "-420605170858861/10000000000000000"},
  {"581211699562287/10000000000000000", "602579091333187/1000000000000000", "-382169975802723/500000000000000"},
  {"-5270246949360691/10000000000000000", "2162861152231751/10000000000000000", "5648167326177671/10000000000000000"},
  {"-4726792748188159/5000000000000000", "1272666794475643/10000000000000000", "4802676718449/16000000000000"},
  {"-1868627957476221/2000000000000000", "12511388959529/1250000000000000", "-420605170858861/10000000000000000"},
  {"-2775631710731053/5000000000000000", "1207418485492689/5000000000000000", "-382169975802723/500000000000000"},
  {"-921399766144207/2500000000000000", "-1085985462790287/2500000000000000", "5648167326177671/10000000000000000"},
  {"-4131696624115331/10000000000000000", "-2149404605253097/2500000000000000", "4802676718449/16000000000000"},
  {"-7455953197761/25000000000000", "-8854924122951479/10000000000000000", "-420605170858861/10000000000000000"},
  {"-4012081174529903/10000000000000000", "-2266669793986531/5000000000000000", "-382169975802723/500000000000000"},
  {"46756585289803/156250000000000", "-4847564861402479/10000000000000000", "5648167326177671/10000000000000000"},
  {"1380011310293969/2000000000000000", "-411642950060219/625000000000000", "4802676718449/16000000000000"},
  {"3749963394741599/5000000000000000", "-5572735187461599/10000000000000000", "-420605170858861/10000000000000000"},
  {"767915222494757/2500000000000000", "-10188661950973/19531250000000", "-382169975802723/500000000000000"},
  {"5535017234623651/10000000000000000", "673991002072371/5000000000000000", "5648167326177671/10000000000000000"},
  {"1679233219444017/2000000000000000", "2263534535574267/5000000000000000", "4802676718449/16000000000000"},
  {"1523518189628179/2000000000000000", "2705392183398847/5000000000000000", "-420605170858861/10000000000000000"},
  {"5910472006450693/10000000000000000", "327326655638497/2500000000000000", "-382169975802723/500000000000000"}
};

// ============================================================================
// EXACT POLYNOMIAL AND AUDIT ARITHMETIC
// ============================================================================

struct QPoly {
  BigRat c[10];
  QPoly() {
    for (int i = 0; i < 10; i++) c[i] = BigRat(0);
  }
  static QPoly MulLinear(const Vec3Q &a, const Vec3Q &b) {
    QPoly p;
    p.c[4] = a.x * b.x;
    p.c[5] = a.x * b.y + a.y * b.x;
    p.c[6] = a.x * b.z + a.z * b.x;
    p.c[7] = a.y * b.y;
    p.c[8] = a.y * b.z + a.z * b.y;
    p.c[9] = a.z * b.z;
    return p;
  }
  void AddScaled(const BigRat &s, const QPoly &o) {
    for (int i = 0; i < 10; i++) c[i] += s * o.c[i];
  }
  std::pair<BigRat, BigRat> EvalCentered(const Vec3Q &center, const Vec3Q &radius) const {
    BigRat val = c[0] + c[1] * center.x + c[2] * center.y + c[3] * center.z +
                 c[4] * center.x * center.x + c[5] * center.x * center.y +
                 c[6] * center.x * center.z + c[7] * center.y * center.y +
                 c[8] * center.y * center.z + c[9] * center.z * center.z;

    BigRat grad_x = c[1] + BigRat(2) * c[4] * center.x + c[5] * center.y + c[6] * center.z;
    BigRat grad_y = c[2] + c[5] * center.x + BigRat(2) * c[7] * center.y + c[8] * center.z;
    BigRat grad_z = c[3] + c[6] * center.x + c[8] * center.y + BigRat(2) * c[9] * center.z;

    auto abs_r = [](const BigRat &x) { return x < 0 ? -x : x; };

    BigRat lin_bound = abs_r(grad_x) * radius.x + abs_r(grad_y) * radius.y + abs_r(grad_z) * radius.z;
    BigRat quad_bound = abs_r(c[4]) * radius.x * radius.x + abs_r(c[5]) * radius.x * radius.y +
                        abs_r(c[6]) * radius.x * radius.z + abs_r(c[7]) * radius.y * radius.y +
                        abs_r(c[8]) * radius.y * radius.z + abs_r(c[9]) * radius.z * radius.z;
    return {val, lin_bound + quad_bound};
  }
};


static Vec3Q GetExactEdge(const ContactInfo &c) {
  Vec3Q v_start(VERTICES_Q[c.edge_start][0], VERTICES_Q[c.edge_start][1], VERTICES_Q[c.edge_start][2]);
  Vec3Q v_finish(VERTICES_Q[c.edge_finish][0], VERTICES_Q[c.edge_finish][1], VERTICES_Q[c.edge_finish][2]);
  Vec3Q v_start2(VERTICES_Q[c.edge_start2][0], VERTICES_Q[c.edge_start2][1], VERTICES_Q[c.edge_start2][2]);
  Vec3Q v_finish2(VERTICES_Q[c.edge_finish2][0], VERTICES_Q[c.edge_finish2][1], VERTICES_Q[c.edge_finish2][2]);
  BigRat mixQ(c.mix, 1000);
  return (v_start - v_finish) * mixQ + (v_start2 - v_finish2) * (BigRat(1) - mixQ);
}

static inline BigRat CeilTo(const BigRat &x, int64_t denom) {
  BigRat scaled = x * BigRat(denom);
  BigInt num = scaled.Numerator();
  BigInt den = scaled.Denominator();
  BigInt q;
  if (num >= BigInt(0)) {
    q = (num + den - BigInt(1)) / den;
  } else {
    q = num / den;
  }
  return BigRat(q, BigInt(denom));
}

static inline BigRat FloorTo(const BigRat &x, int64_t denom) {
  BigRat scaled = x * BigRat(denom);
  BigInt num = scaled.Numerator();
  BigInt den = scaled.Denominator();
  BigInt q;
  if (num >= BigInt(0)) {
    q = num / den;
  } else {
    q = (num - den + BigInt(1)) / den;
  }
  return BigRat(q, BigInt(denom));
}

// Barycentric coordinates of target inside tetrahedron (p0, p1, p2, p3)
static bool Barycentric4(const Vec3Q pts[4], const Vec3Q &target, BigRat lam[4]) {
  Vec3Q v0 = pts[0] - pts[3];
  Vec3Q v1 = pts[1] - pts[3];
  Vec3Q v2 = pts[2] - pts[3];
  Vec3Q w = target - pts[3];
  BigRat den = Vec3Q::Det(v0, v1, v2);
  if (den == 0) return false;
  lam[0] = Vec3Q::Det(w, v1, v2) / den;
  lam[1] = Vec3Q::Det(v0, w, v2) / den;
  lam[2] = Vec3Q::Det(v0, v1, w) / den;
  lam[3] = BigRat(1) - lam[0] - lam[1] - lam[2];
  return true;
}

// Exact tetrahedron axis radius
static BigRat ExactTetrahedronAxisRadius(const Vec3Q pts[4]) {
  BigRat zero[4];
  if (!Barycentric4(pts, {BigRat(0), BigRat(0), BigRat(0)}, zero)) return BigRat(0);
  for (int i = 0; i < 4; i++) {
    if (zero[i] <= 0) return BigRat(0);
  }
  BigRat min_bound(1000000);
  for (int axis = 0; axis < 3; axis++) {
    for (int sign : {-1, 1}) {
      Vec3Q target(axis == 0 ? BigRat(sign) : BigRat(0),
                   axis == 1 ? BigRat(sign) : BigRat(0),
                   axis == 2 ? BigRat(sign) : BigRat(0));
      BigRat at_one[4];
      if (!Barycentric4(pts, target, at_one)) continue;
      for (int i = 0; i < 4; i++) {
        BigRat d = at_one[i] - zero[i];
        if (d < 0) {
          BigRat b = zero[i] / (-d);
          if (b < min_bound) min_bound = b;
        }
      }
    }
  }
  return min_bound;
}

// Audits a single 3-contact axis on triangle tri
static bool AuditAxis(const TriangleQ &tri, ContactInfo contacts[3], AxisCertificate *out_cert,
                      Vec3Q *out_center, BigRat *out_delta, std::string *fail_reason = nullptr) {
  Vec3Q edges[3] = {GetExactEdge(contacts[0]), GetExactEdge(contacts[1]), GetExactEdge(contacts[2])};
  Vec3Q coeff0 = Vec3Q::Cross(edges[1], edges[2]);
  Vec3Q coeff1 = Vec3Q::Cross(edges[2], edges[0]);
  Vec3Q coeff2 = Vec3Q::Cross(edges[0], edges[1]);

  BigRat p0 = Vec3Q::Dot(tri.corners[0], coeff0);
  BigRat p1 = Vec3Q::Dot(tri.corners[0], coeff1);
  BigRat p2 = Vec3Q::Dot(tri.corners[0], coeff2);
  if (p0 < 0 && p1 < 0 && p2 < 0) {
    std::swap(contacts[1], contacts[2]);
    std::swap(edges[1], edges[2]);
    coeff0 = Vec3Q::Cross(edges[1], edges[2]);
    coeff1 = Vec3Q::Cross(edges[2], edges[0]);
    coeff2 = Vec3Q::Cross(edges[0], edges[1]);
  }

  Vec3Q w_coeffs[3] = {coeff0, coeff1, coeff2};
  BigRat support_error(6, 1000000000000000LL); // 6 / 10^15
  BigRat weight_upper[3];
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
      if (fail_reason) *fail_reason = StringPrintf("w_min < support_error (m=%d, w_min=%s)", m, w_min.ToString().c_str());
      return false;
    }
    weight_upper[m] = w_max + support_error;
    sum_weight_upper = sum_weight_upper + weight_upper[m];
  }

  BigRat exact_B = sum_weight_upper * BigRat(2);
  BigRat B = CeilTo(exact_B, 1000000000LL);
  if (B <= 0) {
    if (fail_reason) *fail_reason = "B <= 0";
    return false;
  }

  out_cert->B = B;
  for (int m = 0; m < 3; m++) {
    out_cert->contacts[m] = contacts[m];
    int sel = contacts[m].vertex;
    Vec3Q v_sel(VERTICES_Q[sel][0], VERTICES_Q[sel][1], VERTICES_Q[sel][2]);
    BigRat best_support(1000000);
    int best_w = -1;
    for (int k = 0; k < 20; k++) {
      bool tie = (k == sel) ||
                 (contacts[m].mix == 1000 && sel == contacts[m].edge_finish && k == contacts[m].edge_start) ||
                 (contacts[m].mix == 0 && sel == contacts[m].edge_start2 && k == contacts[m].edge_finish2);
      BigRat s_upper(0);
      if (!tie) {
        Vec3Q v_k(VERTICES_Q[k][0], VERTICES_Q[k][1], VERTICES_Q[k][2]);
        Vec3Q delta = v_k - v_sel;
        Vec3Q s_coeff = Vec3Q::Cross(edges[m], delta);
        BigRat max_s = Vec3Q::Dot(tri.corners[0], s_coeff);
        for (int c = 1; c < 3; c++) {
          BigRat val = Vec3Q::Dot(tri.corners[c], s_coeff);
          if (val > max_s) max_s = val;
        }
        s_upper = max_s + support_error;
      }
      if (s_upper > 0) {
        if (fail_reason) *fail_reason = StringPrintf("s_upper > 0 (m=%d, k=%d, s=%s)", m, k, s_upper.ToString().c_str());
        return false;
      }
      if (s_upper < best_support) {
        best_support = s_upper;
        best_w = k;
      }
    }
    if (best_support >= 0) {
      if (fail_reason) *fail_reason = StringPrintf("best_support >= 0 (m=%d)", m);
      return false;
    }
    out_cert->nonzero_witness[m] = best_w;
  }

  // Bounding box over triangle
  Vec3Q ball_center, ball_radius;
  for (int c = 0; c < 3; c++) {
    BigRat c0 = (c == 0 ? tri.corners[0].x : c == 1 ? tri.corners[0].y : tri.corners[0].z);
    BigRat c1 = (c == 0 ? tri.corners[1].x : c == 1 ? tri.corners[1].y : tri.corners[1].z);
    BigRat c2 = (c == 0 ? tri.corners[2].x : c == 1 ? tri.corners[2].y : tri.corners[2].z);
    BigRat lo = std::min({c0, c1, c2});
    BigRat hi = std::max({c0, c1, c2});
    BigRat mid = (lo + hi) / BigRat(2);
    BigRat rad = (hi - lo) / BigRat(2);
    if (c == 0) { ball_center.x = mid; ball_radius.x = rad; }
    else if (c == 1) { ball_center.y = mid; ball_radius.y = rad; }
    else { ball_center.z = mid; ball_radius.z = rad; }
  }

  QPoly polys[3];
  for (int coord = 0; coord < 3; coord++) {
    for (int i = 0; i < 3; i++) {
      Vec3Q v_supp(VERTICES_Q[contacts[i].vertex][0],
                   VERTICES_Q[contacts[i].vertex][1],
                   VERTICES_Q[contacts[i].vertex][2]);
      Vec3Q lift_row[3] = {
        {BigRat(0), edges[i].z, -edges[i].y},
        {-edges[i].z, BigRat(0), edges[i].x},
        {edges[i].y, -edges[i].x, BigRat(0)}
      };
      Vec3Q cross_lift;
      if (coord == 0) {
        cross_lift = {v_supp.y * lift_row[2].x - v_supp.z * lift_row[1].x,
                      v_supp.y * lift_row[2].y - v_supp.z * lift_row[1].y,
                      v_supp.y * lift_row[2].z - v_supp.z * lift_row[1].z};
      } else if (coord == 1) {
        cross_lift = {v_supp.z * lift_row[0].x - v_supp.x * lift_row[2].x,
                      v_supp.z * lift_row[0].y - v_supp.x * lift_row[2].y,
                      v_supp.z * lift_row[0].z - v_supp.x * lift_row[2].z};
      } else {
        cross_lift = {v_supp.x * lift_row[1].x - v_supp.y * lift_row[0].x,
                      v_supp.x * lift_row[1].y - v_supp.y * lift_row[0].y,
                      v_supp.x * lift_row[1].z - v_supp.y * lift_row[0].z};
      }
      QPoly term = QPoly::MulLinear(w_coeffs[i], cross_lift);
      polys[coord].AddScaled(BigRat(1), term);
    }
  }

  std::pair<BigRat, BigRat> evals[3] = {
    polys[0].EvalCentered(ball_center, ball_radius),
    polys[1].EvalCentered(ball_center, ball_radius),
    polys[2].EvalCentered(ball_center, ball_radius)
  };

  out_center->x = evals[0].first / B;
  out_center->y = evals[1].first / B;
  out_center->z = evals[2].first / B;

  BigRat variation_error(150, 10000000000LL); // 150 * KAPPA
  BigRat exact_delta = (evals[0].second + evals[1].second + evals[2].second + BigRat(3) * variation_error) / B;
  *out_delta = CeilTo(exact_delta, 1000000000LL);
  return true;
}

static const BigRat CANDIDATE_RS[] = {
  BigRat("1/1000"), BigRat("1/2000"), BigRat("1/4000"),
  BigRat("1/5000"), BigRat("1/8000"), BigRat("1/10000"),
  BigRat("1/20000"), BigRat("1/40000"), BigRat("1/50000"),
  BigRat("1/100000"), BigRat("1/200000"), BigRat("1/250000"),
  BigRat("1/300000"), BigRat("1/400000"), BigRat("1/500000"),
  BigRat("1/1000000"), BigRat("1/2000000"), BigRat("1/3000000"),
  BigRat("1/4000000"), BigRat("1/5000000"), BigRat("1/6000000"),
  BigRat("1/8000000"), BigRat("1/10000000"), BigRat("1/20000000"),
  BigRat("1/50000000"), BigRat("1/100000000")
};

static bool AuditCertificateAdaptive(
    const TriangleQ &tri,
    const TubeCertificate &in_cert,
    TubeCertificate *out_cert,
    std::string *fail_reason = nullptr) {

  *out_cert = in_cert;
  Vec3Q centers[4];
  BigRat deltas[4];
  for (int a = 0; a < 4; a++) {
    ContactInfo contacts[3] = {in_cert.axes[a].contacts[0],
                               in_cert.axes[a].contacts[1],
                               in_cert.axes[a].contacts[2]};
    std::string axis_reason;
    if (!AuditAxis(tri, contacts, &out_cert->axes[a], &centers[a], &deltas[a], &axis_reason)) {
      if (fail_reason) *fail_reason = StringPrintf("axis %d audit failed: %s", a, axis_reason.c_str());
      return false;
    }
  }

  BigRat delta = std::max({deltas[0], deltas[1], deltas[2], deltas[3]});
  BigRat axis_radius = ExactTetrahedronAxisRadius(centers);
  BigRat cover_radius = axis_radius * BigRat(19, 20) * BigRat(4, 7);
  BigRat c = FloorTo(cover_radius - delta, 1000000000LL);

  if (c <= BigRat(0)) {
    if (fail_reason) *fail_reason = StringPrintf("c <= 0 (c=%s, cover=%s, delta=%s)",
                                                 c.ToString().c_str(), cover_radius.ToString().c_str(), delta.ToString().c_str());
    return false;
  }

  BigRat scale = (c + delta) * BigRat(7, 4);
  for (int axis = 0; axis < 3; axis++) {
    for (int sign : {-1, 1}) {
      Vec3Q target(axis == 0 ? scale * BigRat(sign) : BigRat(0),
                   axis == 1 ? scale * BigRat(sign) : BigRat(0),
                   axis == 2 ? scale * BigRat(sign) : BigRat(0));
      BigRat lam[4];
      if (!Barycentric4(centers, target, lam)) {
        if (fail_reason) *fail_reason = "Barycentric4 degenerate";
        return false;
      }
      for (int i = 0; i < 4; i++) {
        if (lam[i] < 0) {
          if (fail_reason) *fail_reason = StringPrintf("target axis %d sign %d not inside (lam[%d]=%s)",
                                                       axis, sign, i, lam[i].ToString().c_str());
          return false;
        }
      }
    }
  }

  BigRat certified_r(0);
  for (const auto &cand : CANDIDATE_RS) {
    if (cand * cand * (BigRat(1) + c * c) <= BigRat(4) * c * c) {
      certified_r = cand;
      break;
    }
  }
  if (certified_r <= BigRat(0)) {
    if (fail_reason) *fail_reason = "No candidate radius satisfies angle bound";
    return false;
  }

  out_cert->c = c;
  out_cert->delta = delta;
  out_cert->r = certified_r;
  return true;
}

// ============================================================================
// CANDIDATE GENERATION, WOLFE SOLVER, AND UPPER BOUND ESTIMATION
// ============================================================================

static inline double Cross2(const vec2 &a, const vec2 &b) {
  return a.x * b.y - a.y * b.x;
}

static std::vector<int> ConvexHull2D(const std::vector<vec2> &points) {
  int n = points.size();
  std::vector<int> p(n);
  std::iota(p.begin(), p.end(), 0);
  std::sort(p.begin(), p.end(), [&](int a, int b) {
    if (points[a].x != points[b].x) return points[a].x < points[b].x;
    return points[a].y < points[b].y;
  });

  auto half = [&](const std::vector<int> &indices) {
    std::vector<int> ans;
    for (int idx : indices) {
      while (ans.size() >= 2) {
        vec2 a = points[ans[ans.size() - 2]];
        vec2 b = points[ans[ans.size() - 1]];
        vec2 pt = points[idx];
        vec2 ab = b - a;
        vec2 bp = pt - b;
        if (Cross2(ab, bp) > 1e-14) break;
        ans.pop_back();
      }
      ans.push_back(idx);
    }
    return ans;
  };

  std::vector<int> lower = half(p);
  std::vector<int> rev_p = p;
  std::reverse(rev_p.begin(), rev_p.end());
  std::vector<int> upper = half(rev_p);

  std::vector<int> hull;
  if (!lower.empty()) lower.pop_back();
  if (!upper.empty()) upper.pop_back();
  hull.insert(hull.end(), lower.begin(), lower.end());
  hull.insert(hull.end(), upper.begin(), upper.end());
  return hull;
}

struct CandidateTriple {
  ContactInfo contacts[3];
  vec3 normalized_a;
  double strict_slack = 0.0;
};

static inline vec3 GetDoubleEdge(const ContactInfo &c) {
  vec3 v_start = VERTICES_D[c.edge_start];
  vec3 v_finish = VERTICES_D[c.edge_finish];
  vec3 v_start2 = VERTICES_D[c.edge_start2];
  vec3 v_finish2 = VERTICES_D[c.edge_finish2];
  double mixD = c.mix / 1000.0;
  return (v_start - v_finish) * mixD + (v_start2 - v_finish2) * (1.0 - mixD);
}

static void GenerateCandidatesForView(
    const vec3 &view,
    const TriangleQ &tri,
    bool evaluate_over_triangle,
    int cone_samples,
    bool include_boundaries,
    double screen_support_error,
    std::vector<CandidateTriple> *out_candidates) {

  out_candidates->clear();
  double vlen = yocto::length(view);
  if (vlen < 1e-12) return;
  vec3 unit_view = view / vlen;

  int axis_index = 0;
  double min_abs = 1e30;
  for (int i = 0; i < 3; i++) {
    if (std::abs(unit_view[i]) < min_abs) {
      min_abs = std::abs(unit_view[i]);
      axis_index = i;
    }
  }
  vec3 axis = (axis_index == 0 ? vec3{1, 0, 0} : (axis_index == 1 ? vec3{0, 1, 0} : vec3{0, 0, 1}));
  vec3 first = yocto::cross(unit_view, axis);
  double first_len = yocto::length(first);
  if (first_len < 1e-12) return;
  first = first / first_len;
  vec3 second = yocto::cross(unit_view, first);

  std::vector<vec2> projected(20);
  for (int k = 0; k < 20; k++) {
    projected[k] = vec2{yocto::dot(VERTICES_D[k], first), yocto::dot(VERTICES_D[k], second)};
  }
  std::vector<int> cycle = ConvexHull2D(projected);
  int H = cycle.size();
  if (H < 3) return;

  std::vector<ContactInfo> contacts;
  std::vector<vec3> lifts;
  for (int pos = 0; pos < H; pos++) {
    int vertex = cycle[pos];
    int previous = cycle[(pos - 1 + H) % H];
    int following = cycle[(pos + 1) % H];

    std::vector<double> samples;
    for (int s = 0; s < cone_samples; s++) {
      samples.push_back((s + 1.0) / (cone_samples + 1.0));
    }
    if (include_boundaries) {
      samples.push_back(0.0);
      samples.push_back(0.001);
      samples.push_back(0.999);
      samples.push_back(1.0);
      std::sort(samples.begin(), samples.end());
      samples.erase(std::unique(samples.begin(), samples.end()), samples.end());
    }

    for (double lam : samples) {
      int mix = std::clamp(static_cast<int>(std::round(1000.0 * lam)), 0, 1000);
      ContactInfo ci;
      ci.vertex = vertex;
      ci.edge_start = previous;
      ci.edge_finish = vertex;
      ci.edge_start2 = vertex;
      ci.edge_finish2 = following;
      ci.mix = mix;
      vec3 edge = GetDoubleEdge(ci);
      vec3 lift = yocto::cross(unit_view, edge);
      contacts.push_back(ci);
      lifts.push_back(lift);
    }
  }

  vec3 tri_f[3] = {
    tri.corners[0].ToDouble(),
    tri.corners[1].ToDouble(),
    tri.corners[2].ToDouble()
  };
  vec3 centroid = (tri_f[0] + tri_f[1] + tri_f[2]) / 3.0;

  struct ContactSupportData {
    vec3 edge;
    int selected;
    double strict_slack;
    bool support_ok;
  };

  std::vector<ContactSupportData> supp_cache(contacts.size());
  for (size_t i = 0; i < contacts.size(); i++) {
    const auto &c = contacts[i];
    vec3 edge = GetDoubleEdge(c);
    int sel = c.vertex;
    double strict_slack = 1e30;
    bool ok = true;
    for (int k = 0; k < 20; k++) {
      bool tie = (k == sel) ||
                 (c.mix == 1000 && sel == c.edge_finish && k == c.edge_start) ||
                 (c.mix == 0 && sel == c.edge_start2 && k == c.edge_finish2);
      if (tie) continue;
      vec3 delta = VERTICES_D[k] - VERTICES_D[sel];
      vec3 coeff = yocto::cross(edge, delta);
      double upper = evaluate_over_triangle
          ? std::max({yocto::dot(tri_f[0], coeff), yocto::dot(tri_f[1], coeff), yocto::dot(tri_f[2], coeff)}) + screen_support_error
          : yocto::dot(centroid, coeff) + screen_support_error;
      strict_slack = std::min(strict_slack, -upper);
      if (upper > 0) {
        ok = false;
        break;
      }
    }
    supp_cache[i] = {edge, sel, strict_slack, ok};
  }

  std::vector<int> valid_idx;
  for (size_t i = 0; i < contacts.size(); i++) {
    if (supp_cache[i].support_ok) valid_idx.push_back(i);
  }

  int V = valid_idx.size();

  for (int ii = 0; ii < V; ii++) {
    int i = valid_idx[ii];
    for (int jj = ii + 1; jj < V; jj++) {
      int j = valid_idx[jj];
      for (int kk = jj + 1; kk < V; kk++) {
        int k = valid_idx[kk];

        vec3 lifts_triple[3] = {lifts[i], lifts[j], lifts[k]};
        vec3 w = {yocto::dot(unit_view, yocto::cross(lifts_triple[1], lifts_triple[2])),
                  yocto::dot(unit_view, yocto::cross(lifts_triple[2], lifts_triple[0])),
                  yocto::dot(unit_view, yocto::cross(lifts_triple[0], lifts_triple[1]))};
        if (w[0] <= 1e-12 && w[1] <= 1e-12 && w[2] <= 1e-12) {
          w = -w;
        }
        if (w[0] < -1e-10 || w[1] < -1e-10 || w[2] < -1e-10) continue;

        ContactInfo sel_contacts[3] = {contacts[i], contacts[j], contacts[k]};
        vec3 c_edges[3] = {supp_cache[i].edge, supp_cache[j].edge, supp_cache[k].edge};
        ContactSupportData c_supp[3] = {supp_cache[i], supp_cache[j], supp_cache[k]};

        vec3 probe = {yocto::dot(tri_f[0], yocto::cross(c_edges[1], c_edges[2])),
                      yocto::dot(tri_f[0], yocto::cross(c_edges[2], c_edges[0])),
                      yocto::dot(tri_f[0], yocto::cross(c_edges[0], c_edges[1]))};
        if (std::max({probe[0], probe[1], probe[2]}) < 0) {
          std::swap(sel_contacts[1], sel_contacts[2]);
          std::swap(c_edges[1], c_edges[2]);
          std::swap(c_supp[1], c_supp[2]);
        }

        vec3 weight_coeffs[3] = {
          yocto::cross(c_edges[1], c_edges[2]),
          yocto::cross(c_edges[2], c_edges[0]),
          yocto::cross(c_edges[0], c_edges[1])
        };

        bool weight_ok = true;
        double max_weight_lower = -1e30;
        double weights_at_max[3] = {0, 0, 0};
        for (int m = 0; m < 3; m++) {
          double w0 = yocto::dot(tri_f[0], weight_coeffs[m]);
          double w1 = yocto::dot(tri_f[1], weight_coeffs[m]);
          double w2 = yocto::dot(tri_f[2], weight_coeffs[m]);
          double w_min = std::min({w0, w1, w2}) - screen_support_error;
          double w_max = std::max({w0, w1, w2}) + screen_support_error;
          if (w_min < 0) { weight_ok = false; break; }
          max_weight_lower = std::max(max_weight_lower, w_min);
          weights_at_max[m] = w_max;
        }
        if (!weight_ok || max_weight_lower <= 0) continue;

        double strict_slack = std::min({c_supp[0].strict_slack, c_supp[1].strict_slack, c_supp[2].strict_slack});

        vec3 weights = {yocto::dot(centroid, weight_coeffs[0]),
                        yocto::dot(centroid, weight_coeffs[1]),
                        yocto::dot(centroid, weight_coeffs[2])};
        double B = 2.0 * (weights_at_max[0] + weights_at_max[1] + weights_at_max[2]);
        vec3 variation = {0, 0, 0};
        for (int m = 0; m < 3; m++) {
          vec3 lift = yocto::cross(centroid, c_edges[m]);
          vec3 term = yocto::cross(VERTICES_D[c_supp[m].selected], lift);
          variation = variation + term * weights[m];
        }
        vec3 normalized_a = variation / B;

        CandidateTriple cand;
        cand.contacts[0] = sel_contacts[0];
        cand.contacts[1] = sel_contacts[1];
        cand.contacts[2] = sel_contacts[2];
        cand.normalized_a = normalized_a;
        cand.strict_slack = strict_slack;
        out_candidates->push_back(cand);
      }
    }
  }
}

static bool SolveLinear4D(const double A[4][4], const double b[4], double x[4]) {
  double aug[4][5];
  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 4; c++) aug[r][c] = A[r][c];
    aug[r][4] = b[r];
  }
  for (int col = 0; col < 4; col++) {
    int pivot = col;
    double max_val = std::abs(aug[col][col]);
    for (int r = col + 1; r < 4; r++) {
      if (std::abs(aug[r][col]) > max_val) {
        max_val = std::abs(aug[r][col]);
        pivot = r;
      }
    }
    if (max_val < 1e-12) return false;
    if (pivot != col) {
      for (int c = 0; c < 5; c++) std::swap(aug[col][c], aug[pivot][c]);
    }
    double div = aug[col][col];
    for (int c = 0; c < 5; c++) aug[col][c] /= div;
    for (int r = 0; r < 4; r++) {
      if (r == col) continue;
      double mult = aug[r][col];
      for (int c = 0; c < 5; c++) aug[r][c] -= mult * aug[col][c];
    }
  }
  for (int r = 0; r < 4; r++) x[r] = aug[r][4];
  return true;
}

static bool TetrahedronOriginMargin(const vec3 pts[4], double *margin, double bary[4]) {
  double A[4][4];
  for (int col = 0; col < 4; col++) {
    A[0][col] = pts[col].x;
    A[1][col] = pts[col].y;
    A[2][col] = pts[col].z;
    A[3][col] = 1.0;
  }
  double b[4] = {0.0, 0.0, 0.0, 1.0};
  if (!SolveLinear4D(A, b, bary)) return false;
  if (bary[0] <= 1e-10 || bary[1] <= 1e-10 || bary[2] <= 1e-10 || bary[3] <= 1e-10) return false;

  double min_face_dist = 1e30;
  for (int omitted = 0; omitted < 4; omitted++) {
    vec3 face[3];
    int idx = 0;
    for (int i = 0; i < 4; i++) {
      if (i != omitted) face[idx++] = pts[i];
    }
    vec3 normal = yocto::cross(face[1] - face[0], face[2] - face[0]);
    double len = yocto::length(normal);
    if (len < 1e-12) return false;
    double d = std::abs(yocto::dot(normal, face[0])) / len;
    min_face_dist = std::min(min_face_dist, d);
  }
  *margin = min_face_dist;
  return true;
}

struct ClosestResult {
  double key = 1e30;
  std::vector<int> support;
  vec3 point = {0, 0, 0};
};

static ClosestResult ClosestOriginFace(const std::vector<vec3> &pts, const std::vector<int> &indices) {
  ClosestResult best{1e30, {}, {0, 0, 0}};

  auto consider = [&](const std::vector<int> &supp, const std::vector<double> &coeffs) {
    vec3 pt = {0, 0, 0};
    for (size_t i = 0; i < supp.size(); i++) {
      pt = pt + pts[supp[i]] * coeffs[i];
    }
    double k = yocto::dot(pt, pt);
    if (k < best.key) {
      best = {k, supp, pt};
    }
  };

  for (int idx : indices) consider({idx}, {1.0});

  int m = indices.size();
  for (int i = 0; i < m; i++) {
    for (int j = i + 1; j < m; j++) {
      int idx_a = indices[i], idx_b = indices[j];
      vec3 a = pts[idx_a], b = pts[idx_b];
      vec3 dir = b - a;
      double den = yocto::dot(dir, dir);
      if (den <= 1e-30) continue;
      double t = -yocto::dot(a, dir) / den;
      if (t > 1e-12 && t < 1.0 - 1e-12) {
        consider({idx_a, idx_b}, {1.0 - t, t});
      }
    }
  }

  for (int i = 0; i < m; i++) {
    for (int j = i + 1; j < m; j++) {
      for (int k = j + 1; k < m; k++) {
        int idx_a = indices[i], idx_b = indices[j], idx_c = indices[k];
        vec3 a = pts[idx_a], b = pts[idx_b], c = pts[idx_c];
        vec3 u = b - a;
        vec3 v = c - a;
        double uu = yocto::dot(u, u), uv = yocto::dot(u, v), vv = yocto::dot(v, v);
        double det = uu * vv - uv * uv;
        if (det <= 1e-30) continue;
        double au = yocto::dot(a, u), av = yocto::dot(a, v);
        double s = (-au * vv + av * uv) / det;
        double t = (-av * uu + au * uv) / det;
        if (s > 1e-12 && t > 1e-12 && (s + t) < 1.0 - 1e-12) {
          consider({idx_a, idx_b, idx_c}, {1.0 - s - t, s, t});
        }
      }
    }
  }
  return best;
}

static bool FindBalancedTetrahedron(const std::vector<vec3> &pts, std::array<int, 4> *out_indices) {
  int n = pts.size();
  if (n < 4) return false;

  int start = 0;
  double min_norm = yocto::dot(pts[0], pts[0]);
  for (int i = 1; i < n; i++) {
    double d = yocto::dot(pts[i], pts[i]);
    if (d < min_norm) { min_norm = d; start = i; }
  }

  std::vector<int> active = {start};
  std::set<std::pair<std::vector<int>, int>> seen;

  for (int iter = 0; iter < 100; iter++) {
    if (active.size() >= 4) {
      int m = active.size();
      for (int i = 0; i < m; i++) {
        for (int j = i + 1; j < m; j++) {
          for (int k = j + 1; k < m; k++) {
            for (int l = k + 1; l < m; l++) {
              vec3 tpts[4] = {pts[active[i]], pts[active[j]], pts[active[k]], pts[active[l]]};
              double margin;
              double bary[4];
              if (TetrahedronOriginMargin(tpts, &margin, bary)) {
                *out_indices = {active[i], active[j], active[k], active[l]};
                return true;
              }
            }
          }
        }
      }
    }

    ClosestResult closest = ClosestOriginFace(pts, active);
    if (closest.support.empty()) return false;
    active = closest.support;
    vec3 current = closest.point;
    double norm_sq = closest.key;

    int next_index = -1;
    double min_dot = 1e30;
    for (int i = 0; i < n; i++) {
      double d = yocto::dot(current, pts[i]);
      if (d < min_dot) { min_dot = d; next_index = i; }
    }
    double improvement = norm_sq - min_dot;
    if (improvement <= 1e-13 * std::max(1.0, norm_sq)) return false;

    if (std::find(active.begin(), active.end(), next_index) != active.end()) return false;

    std::vector<int> sorted_active = active;
    std::sort(sorted_active.begin(), sorted_active.end());
    if (seen.count({sorted_active, next_index})) return false;
    seen.insert({sorted_active, next_index});
    active.push_back(next_index);
  }
  return false;
}

static bool FindExtremalTetrahedron(const std::vector<vec3> &pts, std::array<int, 4> *out_indices) {
  int n = pts.size();
  if (n < 4) return false;
  const double inv_sqrt3 = 1.0 / std::sqrt(3.0);
  const vec3 base_t[4] = {
    { inv_sqrt3,  inv_sqrt3,  inv_sqrt3},
    { inv_sqrt3, -inv_sqrt3, -inv_sqrt3},
    {-inv_sqrt3,  inv_sqrt3, -inv_sqrt3},
    {-inv_sqrt3, -inv_sqrt3,  inv_sqrt3}
  };

  std::mt19937 rng(12345);
  std::normal_distribution<double> gauss(0.0, 1.0);

  for (int trial = 0; trial < 400; trial++) {
    vec3 v0 = {gauss(rng), gauss(rng), gauss(rng)};
    double l0 = yocto::length(v0);
    if (l0 < 1e-6) continue;
    v0 = v0 / l0;

    vec3 v1 = {gauss(rng), gauss(rng), gauss(rng)};
    v1 = v1 - v0 * yocto::dot(v0, v1);
    double l1 = yocto::length(v1);
    if (l1 < 1e-6) continue;
    v1 = v1 / l1;

    vec3 v2 = yocto::cross(v0, v1);

    vec3 u[4];
    for (int k = 0; k < 4; k++) {
      u[k] = v0 * base_t[k].x + v1 * base_t[k].y + v2 * base_t[k].z;
    }

    int idx[4];
    bool dup = false;
    for (int k = 0; k < 4; k++) {
      double max_dot = -1e30;
      int best_i = -1;
      for (int i = 0; i < n; i++) {
        double d = yocto::dot(pts[i], u[k]);
        if (d > max_dot) { max_dot = d; best_i = i; }
      }
      idx[k] = best_i;
      for (int prev = 0; prev < k; prev++) {
        if (idx[prev] == best_i) { dup = true; break; }
      }
      if (dup) break;
    }
    if (dup) continue;

    vec3 tpts[4] = {pts[idx[0]], pts[idx[1]], pts[idx[2]], pts[idx[3]]};
    double margin;
    double bary[4];
    if (TetrahedronOriginMargin(tpts, &margin, bary)) {
      *out_indices = {idx[0], idx[1], idx[2], idx[3]};
      return true;
    }
  }
  return false;
}

static void EstimateUpperBounds(
    const TriangleQ &tri,
    const std::vector<vec3> &pts,
    BigRat *out_c_upper,
    BigRat *out_r_upper) {

  if (pts.empty()) {
    *out_c_upper = BigRat(0);
    *out_r_upper = BigRat(0);
    return;
  }

  double min_support = 1e30;
  std::mt19937 rng(42);
  std::normal_distribution<double> gauss(0.0, 1.0);
  vec3 worst_u = {0, 0, 0};
  for (int s = 0; s < 1500; s++) {
    vec3 u = {gauss(rng), gauss(rng), gauss(rng)};
    double len = yocto::length(u);
    if (len < 1e-12) continue;
    u = u / len;
    double max_dot = -1e30;
    for (const auto &p : pts) max_dot = std::max(max_dot, yocto::dot(u, p));
    if (max_dot < min_support) {
      min_support = max_dot;
      worst_u = u;
    }
  }
  for (int step = 0; step < 500; step++) {
    vec3 pert = {gauss(rng) * 0.05, gauss(rng) * 0.05, gauss(rng) * 0.05};
    vec3 u = worst_u + pert;
    double len = yocto::length(u);
    if (len < 1e-12) continue;
    u = u / len;
    double max_dot = -1e30;
    for (const auto &p : pts) max_dot = std::max(max_dot, yocto::dot(u, p));
    if (max_dot < min_support) {
      min_support = max_dot;
      worst_u = u;
    }
  }

  vec3 c0 = tri.corners[0].ToDouble();
  vec3 c1 = tri.corners[1].ToDouble();
  vec3 c2 = tri.corners[2].ToDouble();
  double diam = std::max({yocto::length(c0 - c1), yocto::length(c1 - c2), yocto::length(c2 - c0)});

  // c_max <= 0.542857 * max(0, min_support) + 0.52 * diam
  double c_max_d = 0.542857 * std::max(0.0, min_support) + 0.52 * diam;
  if (c_max_d < 0.0) c_max_d = 0.0;

  double r_max_d = (c_max_d < 0.99) ? (2.0 * c_max_d / std::sqrt(1.0 - c_max_d * c_max_d)) : 2.0;

  int64_t c_int = static_cast<int64_t>(std::ceil(c_max_d * 100000000.0));
  *out_c_upper = BigRat(c_int, 100000000LL);

  int64_t r_int = static_cast<int64_t>(std::ceil(r_max_d * 100000000.0));
  *out_r_upper = BigRat(r_int, 100000000LL);
}

// ----------------------------------------------------------------------------
// Thread-safe Certificate Cache
// ----------------------------------------------------------------------------

class CertificateCache {
 public:
  void Insert(const TubeCertificate &cert) {
    std::lock_guard<std::mutex> lock(mu_);
    cache_.push_back(cert);
    if (cache_.size() > 500) {
      cache_.erase(cache_.begin(), cache_.begin() + 100);
    }
  }

  std::vector<TubeCertificate> GetRecent() const {
    std::lock_guard<std::mutex> lock(mu_);
    return cache_;
  }

 private:
  mutable std::mutex mu_;
  std::vector<TubeCertificate> cache_;
};

static bool SynthesizeCertificate(
    const TriangleQ &tri,
    int depth,
    const BigRat &target_c,
    const BigRat &target_r,
    TubeCertificate *out_cert) {

  vec3 tri_f[3] = {
    tri.corners[0].ToDouble(),
    tri.corners[1].ToDouble(),
    tri.corners[2].ToDouble()
  };
  vec3 centroid = (tri_f[0] + tri_f[1] + tri_f[2]) / 3.0;

  struct Pass {
    int cone_samples;
    bool include_boundaries;
    double screen_support_error;
  };
  std::vector<Pass> passes;
  passes.push_back({4, false, 2e-14});
  if (depth >= 4) {
    passes.push_back({4, true, 2e-14});
  }
  if (depth >= 6) {
    passes.push_back({5, true, 2e-14});
  }

  for (const auto &pass : passes) {
    std::vector<CandidateTriple> all_cands;
    std::vector<vec3> sample_views = {centroid, tri_f[0], tri_f[1], tri_f[2]};
    for (const auto &sv : sample_views) {
      std::vector<CandidateTriple> v_cands;
      GenerateCandidatesForView(sv, tri, /*evaluate_over_triangle=*/true, pass.cone_samples, pass.include_boundaries, pass.screen_support_error, &v_cands);
      all_cands.insert(all_cands.end(), v_cands.begin(), v_cands.end());
    }
    if (all_cands.size() < 4) continue;

    // Deduplicate
    std::vector<CandidateTriple> candidates;
    std::vector<vec3> pts;
    for (const auto &c : all_cands) {
      bool dup = false;
      for (const auto &ex : pts) {
        if (yocto::length(ex - c.normalized_a) < 1e-6) { dup = true; break; }
      }
      if (!dup) {
        pts.push_back(c.normalized_a);
        candidates.push_back(c);
      }
    }
    if (pts.size() < 4) continue;

    // Try Extremal regular tetrahedron
    std::array<int, 4> ext_idx;
    if (FindExtremalTetrahedron(pts, &ext_idx)) {
      TubeCertificate test_cert;
      for (int a = 0; a < 4; a++) {
        test_cert.axes[a].contacts[0] = candidates[ext_idx[a]].contacts[0];
        test_cert.axes[a].contacts[1] = candidates[ext_idx[a]].contacts[1];
        test_cert.axes[a].contacts[2] = candidates[ext_idx[a]].contacts[2];
      }
      if (AuditCertificateAdaptive(tri, test_cert, out_cert)) {
        return true;
      }
    }

    // Try Wolfe balanced tetrahedron
    std::array<int, 4> wolfe_idx;
    if (FindBalancedTetrahedron(pts, &wolfe_idx)) {
      TubeCertificate test_cert;
      for (int a = 0; a < 4; a++) {
        test_cert.axes[a].contacts[0] = candidates[wolfe_idx[a]].contacts[0];
        test_cert.axes[a].contacts[1] = candidates[wolfe_idx[a]].contacts[1];
        test_cert.axes[a].contacts[2] = candidates[wolfe_idx[a]].contacts[2];
      }
      if (AuditCertificateAdaptive(tri, test_cert, out_cert)) {
        return true;
      }
    }

    // 3. Combinatorial exploration over exposed extreme points and ranked origin enclosing tetrahedra
    std::set<int> pool_set;
    std::mt19937 rng(1337 + depth * 31);
    std::normal_distribution<double> gauss(0.0, 1.0);
    for (int d = 0; d < 256; d++) {
      vec3 dir = {gauss(rng), gauss(rng), gauss(rng)};
      int best_hi = 0, best_lo = 0;
      double max_v = yocto::dot(pts[0], dir);
      double min_v = max_v;
      for (size_t i = 1; i < pts.size(); i++) {
        double v = yocto::dot(pts[i], dir);
        if (v > max_v) { max_v = v; best_hi = i; }
        if (v < min_v) { min_v = v; best_lo = i; }
      }
      pool_set.insert(best_hi);
      pool_set.insert(best_lo);
    }
    std::vector<int> pool(pool_set.begin(), pool_set.end());

    struct ScoredTet {
      double margin;
      std::array<int, 4> indices;
    };
    std::vector<ScoredTet> scored;

    int P = pool.size();
    if (P >= 4) {
      uint64_t comb4 = (uint64_t)P * (P - 1) * (P - 2) * (P - 3) / 24;
      if (comb4 <= 5000) {
        for (int i = 0; i < P; i++) {
          for (int j = i + 1; j < P; j++) {
            for (int k = j + 1; k < P; k++) {
              for (int l = k + 1; l < P; l++) {
                vec3 tpts[4] = {pts[pool[i]], pts[pool[j]], pts[pool[k]], pts[pool[l]]};
                double margin;
                double bary[4];
                if (TetrahedronOriginMargin(tpts, &margin, bary)) {
                  scored.push_back({margin, {pool[i], pool[j], pool[k], pool[l]}});
                }
              }
            }
          }
        }
      } else {
        std::uniform_int_distribution<int> dist(0, P - 1);
        for (int trial = 0; trial < 10000; trial++) {
          int i0 = pool[dist(rng)], i1 = pool[dist(rng)], i2 = pool[dist(rng)], i3 = pool[dist(rng)];
          if (i0 == i1 || i0 == i2 || i0 == i3 || i1 == i2 || i1 == i3 || i2 == i3) continue;
          vec3 tpts[4] = {pts[i0], pts[i1], pts[i2], pts[i3]};
          double margin;
          double bary[4];
          if (TetrahedronOriginMargin(tpts, &margin, bary)) {
            scored.push_back({margin, {i0, i1, i2, i3}});
          }
        }
      }
    }

    if (!scored.empty()) {
      std::sort(scored.begin(), scored.end(), [](const ScoredTet &a, const ScoredTet &b) {
        return a.margin > b.margin;
      });
      int num_to_audit = std::min((int)scored.size(), 15);
      for (int idx = 0; idx < num_to_audit; idx++) {
        TubeCertificate test_cert;
        for (int a = 0; a < 4; a++) {
          int cand_idx = scored[idx].indices[a];
          test_cert.axes[a].contacts[0] = candidates[cand_idx].contacts[0];
          test_cert.axes[a].contacts[1] = candidates[cand_idx].contacts[1];
          test_cert.axes[a].contacts[2] = candidates[cand_idx].contacts[2];
        }
        if (AuditCertificateAdaptive(tri, test_cert, out_cert)) {
          return true;
        }
      }
    }
  }
  return false;
}

// ============================================================================
// INCREMENTAL BFS TUBE SEARCH MANAGER
// ============================================================================

class IncrementalTubeManager {
 public:
  IncrementalTubeManager(
      std::string root_path,
      int max_depth,
      BigRat target_r,
      BigRat target_c,
      int num_workers,
      std::string output_dir,
      bool revalidate = false)
      : root_path_(std::move(root_path)),
        max_depth_(max_depth),
        target_r_(std::move(target_r)),
        target_c_(std::move(target_c)),
        num_workers_(num_workers),
        output_dir_(std::move(output_dir)),
        revalidate_(revalidate) {

    std::filesystem::create_directories(output_dir_);
  }

  void Run() {
    std::string main_file = SubtreeFilename(root_path_, output_dir_);
    
    if (std::filesystem::exists(main_file)) {
      std::cout << ACYAN("Resuming tree from ") << main_file << "...\n";
      root_ = LoadTreeJson(main_file, /*load_external=*/false, output_dir_);
      if (revalidate_ && root_) {
        std::cout << ACYAN("Revalidating existing certificates across tree...\n");
        int kept = 0, discarded = 0;
        auto reval = [&](auto &self, TreeNode *node) -> void {
          if (!node) return;
          if (node->direct_cert.has_value()) {
            TriangleQ tri = node->GetTriangle();
            TubeCertificate audited;
            if (AuditCertificateAdaptive(tri, *node->direct_cert, &audited)) {
              node->direct_cert = audited;
              node->direct_bounds.direct_r_lower = audited.r;
              node->direct_bounds.direct_c_lower = audited.c;
              cache_.Insert(audited);
              kept++;
            } else {
              node->direct_cert.reset();
              node->direct_bounds.direct_r_lower = BigRat(0);
              node->direct_bounds.direct_c_lower = BigRat(0);
              discarded++;
            }
          }
          for (auto &c : node->children) {
            self(self, c.get());
          }
        };
        reval(reval, root_.get());
        std::cout << "Revalidation finished: kept " << kept << " valid certs, invalidated "
                  << discarded << " stale certs.\n";
      }
    }

    if (!root_) {
      root_ = std::make_unique<TreeNode>();
      root_->path = root_path_;
    }

    std::unordered_map<std::string, TreeNode*> node_map;
    std::deque<std::string> queue;

    auto collect_nodes = [&](auto &self, TreeNode *node) -> void {
      if (!node) return;
      node_map[node->path] = node;
      if (node->direct_cert.has_value()) {
        cache_.Insert(*node->direct_cert);
      }
      if (node->children.empty() && !node->external) {
        bool target_achieved = false;
        if (target_r_ > BigRat(0) && target_c_ > BigRat(0)) {
          target_achieved = node->direct_cert.has_value() &&
                            node->direct_cert->r >= target_r_ &&
                            node->direct_cert->c >= target_c_;
        } else {
          target_achieved = node->direct_cert.has_value() &&
                            node->direct_cert->r > BigRat(0) &&
                            node->direct_cert->c > BigRat(0);
        }
        bool infeasible = false;
        if (target_c_ > BigRat(0)) {
          infeasible = node->direct_bounds.direct_c_upper > BigRat(0) &&
                       node->direct_bounds.direct_c_upper < target_c_;
        }
        if (!target_achieved && !infeasible && node->depth() < max_depth_) {
          queue.push_back(node->path);
        }
      } else {
        for (const auto &child : node->children) {
          self(self, child.get());
        }
      }
    };
    collect_nodes(collect_nodes, root_.get());

    if (queue.empty() && root_->children.empty()) {
      queue.push_back(root_->path);
    }

    std::cout << ACYAN("Starting Incremental BFS Tube Search on ") << num_workers_ << " threads\n"
              << "Root Path: \"" << root_path_ << "\"\n"
              << "Target Radius: " << target_r_.ToString() << "\n"
              << "Target Margin: " << target_c_.ToString() << "\n"
              << "Max Depth: " << max_depth_ << "\n"
              << "Initial Queue: " << queue.size() << " nodes\n\n" << std::flush;

    std::mutex queue_mu;
    std::condition_variable cv;
    bool done = false;
    int busy_workers = 0;
    std::atomic<int64_t> total_processed{0};
    std::atomic<int64_t> count_target_met{0};
    std::atomic<int64_t> count_pruned{0};
    std::atomic<int64_t> count_subdivided{0};

    auto worker_func = [&](int tid) {
      while (true) {
        std::string cur_path;
        TreeNode *node = nullptr;
        {
          std::unique_lock<std::mutex> lock(queue_mu);
          while (queue.empty()) {
            if (busy_workers == 0) {
              done = true;
              cv.notify_all();
              return;
            }
            cv.wait(lock);
            if (done) return;
          }
          cur_path = queue.front();
          queue.pop_front();
          node = node_map[cur_path];
          busy_workers++;
        }

        if (!node) {
          std::lock_guard<std::mutex> lock(queue_mu);
          busy_workers--;
          continue;
        }
        TriangleQ tri = node->GetTriangle();

        bool meets_target = false;
        bool infeasible = false;

        // 1. Audit existing cache or synthesize direct certificate
        TubeCertificate cert;
        bool cert_found = false;
        std::vector<TubeCertificate> recent = cache_.GetRecent();
        for (const auto &rc : recent) {
          if (AuditCertificateAdaptive(tri, rc, &cert)) {
            cert_found = true;
            break;
          }
        }

        if (!cert_found) {
          if (SynthesizeCertificate(tri, node->depth(), target_c_, target_r_, &cert)) {
            cert_found = true;
          }
        }

        if (cert_found) {
          node->direct_cert = cert;
          node->direct_bounds.direct_r_lower = cert.r;
          node->direct_bounds.direct_c_lower = cert.c;
          cache_.Insert(cert);
        }

        if (target_r_ > BigRat(0) && target_c_ > BigRat(0)) {
          meets_target = (cert_found && cert.r >= target_r_ && cert.c >= target_c_);
        } else {
          // Any strictly positive bound meets target!
          meets_target = (cert_found && cert.r > BigRat(0) && cert.c > BigRat(0));
        }

        // 2. If target not met, compute upper bounds to test for infeasibility
        if (!meets_target) {
          if (node->direct_bounds.direct_c_upper > BigRat(0)) {
            if (target_c_ > BigRat(0)) {
              infeasible = (node->direct_bounds.direct_c_upper < target_c_);
            } else {
              infeasible = (node->direct_bounds.direct_c_upper <= BigRat(0));
            }
          } else {
            vec3 centroid = (tri.corners[0].ToDouble() + tri.corners[1].ToDouble() + tri.corners[2].ToDouble()) / 3.0;
            std::vector<CandidateTriple> candidates;
            GenerateCandidatesForView(centroid, tri, /*evaluate_over_triangle=*/false, 4, true, 2e-14, &candidates);
            std::vector<vec3> pts;
            pts.reserve(candidates.size());
            for (const auto &c : candidates) pts.push_back(c.normalized_a);

            BigRat c_upper(0), r_upper(0);
            EstimateUpperBounds(tri, pts, &c_upper, &r_upper);
            node->direct_bounds.direct_c_upper = c_upper;
            node->direct_bounds.direct_r_upper = r_upper;

            if (target_c_ > BigRat(0)) {
              infeasible = (c_upper > BigRat(0) && c_upper < target_c_);
            } else {
              infeasible = (c_upper <= BigRat(0));
            }
          }
        }

        // 3. Dual-bound evaluation and subdivision decision
        bool max_depth_reached = (node->depth() >= max_depth_);

        if (meets_target) {
          count_target_met++;
        } else if (infeasible) {
          count_pruned++;
        } else if (max_depth_reached) {
          // Bounded leaf at depth cap
        } else {
          // Subdivide!
          count_subdivided++;
          std::vector<std::string> new_paths;
          {
            std::lock_guard<std::mutex> lock(queue_mu);
            node->children.reserve(4);
            for (int i = 0; i < 4; i++) {
              auto child = std::make_unique<TreeNode>();
              child->path = cur_path + std::to_string(i);
              node_map[child->path] = child.get();
              new_paths.push_back(child->path);
              node->children.push_back(std::move(child));
            }
            for (const auto &np : new_paths) {
              queue.push_back(np);
            }
            cv.notify_all();
          }
        }

        int64_t processed = ++total_processed;
        if (processed % 100 == 0) {
          std::cout << std::format(
            "[{}] Processed: {} | Queue: {} | Met Target: {} | Pruned: {} | Subdivided: {}\n",
            cur_path, processed, queue.size(), count_target_met.load(), count_pruned.load(), count_subdivided.load()
          ) << std::flush;
        }

        {
          std::lock_guard<std::mutex> lock(queue_mu);
          busy_workers--;
          if (queue.empty() && busy_workers == 0) {
            done = true;
            cv.notify_all();
            return;
          }
        }
      }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < num_workers_; i++) {
      threads.emplace_back(worker_func, i);
    }
    for (auto &t : threads) {
      if (t.joinable()) t.join();
    }

    // Final save
    SaveTreeJson(*root_, main_file, /*shallow=*/false);
    TreeStats stats = ComputeTreeStats(*root_);
    EffectiveBounds eb = ComputeEffectiveBounds(*root_);

    std::cout << "\n" << AGREEN("=== INCREMENTAL SEARCH COMPLETE ===") << "\n"
              << "Main Tree File: " << main_file << "\n"
              << "Total Nodes: " << stats.total_nodes << " (Leaves: " << stats.leaves
              << ", Direct Certs: " << stats.direct_certificates << ")\n"
              << "Max Depth: " << stats.max_depth << "\n"
              << "Root Effective r_lower: " << eb.r_lower.ToString() << "\n"
              << "Root Effective r_upper: " << eb.r_upper.ToString() << "\n"
              << "Complete Coverage: " << (eb.complete ? "YES" : "INCOMPLETE") << "\n\n";
  }

 private:
  std::string root_path_;
  int max_depth_;
  BigRat target_r_;
  BigRat target_c_;
  int num_workers_;
  std::string output_dir_;
  bool revalidate_ = false;

  std::unique_ptr<TreeNode> root_;
  CertificateCache cache_;
};

static void DiagnosePath(const std::string &path) {
  std::cout << "=== DIAGNOSING PATH: \"" << path << "\" ===\n";
  TriangleQ tri = TriangleFromPath(path);
  std::cout << "Corners:\n";
  for (int i = 0; i < 3; i++) {
    std::cout << "  c[" << i << "] = ("
              << tri.corners[i].x.ToString() << ", "
              << tri.corners[i].y.ToString() << ", "
              << tri.corners[i].z.ToString() << ")"
              << " (" << tri.corners[i].ToDouble().x << ", "
              << tri.corners[i].ToDouble().y << ", "
              << tri.corners[i].ToDouble().z << ")\n";
  }
  vec3 tri_f[3] = {tri.corners[0].ToDouble(), tri.corners[1].ToDouble(), tri.corners[2].ToDouble()};
  vec3 centroid = (tri_f[0] + tri_f[1] + tri_f[2]) / 3.0;
  std::cout << "Centroid: (" << centroid.x << ", " << centroid.y << ", " << centroid.z << ")\n";
  double diam = std::max({yocto::length(tri_f[0] - tri_f[1]), yocto::length(tri_f[1] - tri_f[2]), yocto::length(tri_f[2] - tri_f[0])});
  std::cout << "Diameter: " << diam << " rad\n";

  // Upper bounds
  std::vector<CandidateTriple> candidates;
  GenerateCandidatesForView(centroid, tri, false, 4, true, 2e-14, &candidates);
  std::cout << "Centroid candidate count (unconstrained): " << candidates.size() << "\n";
  std::vector<vec3> pts;
  for (const auto &c : candidates) pts.push_back(c.normalized_a);
  BigRat c_upper, r_upper;
  EstimateUpperBounds(tri, pts, &c_upper, &r_upper);
  std::cout << "Estimated c_upper: " << c_upper.ToString() << " (" << c_upper.ToDouble() << ")\n";
  std::cout << "Estimated r_upper: " << r_upper.ToString() << " (" << r_upper.ToDouble() << ")\n";

  // Check passes
  struct Pass {
    int cone_samples;
    bool include_boundaries;
    double screen_support_error;
  };
  std::vector<Pass> passes = {
    {4, false, 2e-14},
    {4, true, 2e-14},
    {5, true, 2e-14},
    {6, true, 2e-14}
  };

  for (size_t p = 0; p < passes.size(); p++) {
    const auto &pass = passes[p];
    std::cout << "\n--- Pass " << p << " (cone=" << pass.cone_samples
              << ", bdry=" << pass.include_boundaries << ") ---\n";
    std::vector<CandidateTriple> all_cands;
    std::vector<vec3> sample_views = {centroid, tri_f[0], tri_f[1], tri_f[2]};
    for (size_t s = 0; s < sample_views.size(); s++) {
      std::vector<CandidateTriple> v_cands;
      GenerateCandidatesForView(sample_views[s], tri, true, pass.cone_samples, pass.include_boundaries, pass.screen_support_error, &v_cands);
      std::cout << "  Sample view " << s << " generated " << v_cands.size() << " valid candidates over tri.\n";
      all_cands.insert(all_cands.end(), v_cands.begin(), v_cands.end());
    }

    std::vector<CandidateTriple> cand_dedup;
    std::vector<vec3> pts_dedup;
    for (const auto &c : all_cands) {
      bool dup = false;
      for (const auto &ex : pts_dedup) {
        if (yocto::length(ex - c.normalized_a) < 1e-6) { dup = true; break; }
      }
      if (!dup) {
        pts_dedup.push_back(c.normalized_a);
        cand_dedup.push_back(c);
      }
    }
    std::cout << "  Total unique candidates: " << cand_dedup.size() << "\n";
    vec3 min_p = pts_dedup[0], max_p = pts_dedup[0];
    for (const auto &p : pts_dedup) {
      min_p = {std::min(min_p.x, p.x), std::min(min_p.y, p.y), std::min(min_p.z, p.z)};
      max_p = {std::max(max_p.x, p.x), std::max(max_p.y, p.y), std::max(max_p.z, p.z)};
    }
    std::cout << "  Bounding box of pts_dedup: X ["
              << min_p.x << ", " << max_p.x << "] Y ["
              << min_p.y << ", " << max_p.y << "] Z ["
              << min_p.z << ", " << max_p.z << "]\n";
    double min_supp = 1e30;
    std::mt19937 r_test(42);
    std::normal_distribution<double> g_test(0.0, 1.0);
    for (int s = 0; s < 5000; s++) {
      vec3 u = {g_test(r_test), g_test(r_test), g_test(r_test)};
      double len = yocto::length(u);
      if (len < 1e-12) continue;
      u = u / len;
      double max_dot = -1e30;
      for (const auto &p : pts_dedup) max_dot = std::max(max_dot, yocto::dot(u, p));
      min_supp = std::min(min_supp, max_dot);
    }
    std::cout << "  pts_dedup min_supp: " << min_supp << "\n";
    BigRat dedup_c_upper, dedup_r_upper;
    EstimateUpperBounds(tri, pts_dedup, &dedup_c_upper, &dedup_r_upper);
    std::cout << "  Convex hull of valid tri candidates: c_upper = "
              << dedup_c_upper.ToString() << " (" << dedup_c_upper.ToDouble() << ")\n";
    if (cand_dedup.size() < 4) {
      std::cout << "  Insufficient candidates (< 4) for tet enclosure.\n";
      continue;
    }

    // 1. Try Extremal
    std::array<int, 4> ext_idx;
    if (FindExtremalTetrahedron(pts_dedup, &ext_idx)) {
      std::cout << "  FindExtremalTetrahedron found tet: indices ["
                << ext_idx[0] << ", " << ext_idx[1] << ", " << ext_idx[2] << ", " << ext_idx[3] << "]\n";
      TubeCertificate test_cert;
      for (int a = 0; a < 4; a++) {
        test_cert.axes[a].contacts[0] = cand_dedup[ext_idx[a]].contacts[0];
        test_cert.axes[a].contacts[1] = cand_dedup[ext_idx[a]].contacts[1];
        test_cert.axes[a].contacts[2] = cand_dedup[ext_idx[a]].contacts[2];
      }
      TubeCertificate out_cert;
      std::string fail_reason;
      if (AuditCertificateAdaptive(tri, test_cert, &out_cert, &fail_reason)) {
        std::cout << AGREEN("  Extremal tet SUCCESS! c = ") << out_cert.c.ToString() << ", r = " << out_cert.r.ToString() << "\n";
        return;
      } else {
        std::cout << "  Extremal tet audit failed: " << fail_reason << "\n";
      }
    } else {
      std::cout << "  FindExtremalTetrahedron failed to find enclosing tet.\n";
    }

    // 2. Try Balanced
    std::array<int, 4> wolfe_idx;
    if (FindBalancedTetrahedron(pts_dedup, &wolfe_idx)) {
      std::cout << "  FindBalancedTetrahedron found tet: indices ["
                << wolfe_idx[0] << ", " << wolfe_idx[1] << ", " << wolfe_idx[2] << ", " << wolfe_idx[3] << "]\n";
      TubeCertificate test_cert;
      for (int a = 0; a < 4; a++) {
        test_cert.axes[a].contacts[0] = cand_dedup[wolfe_idx[a]].contacts[0];
        test_cert.axes[a].contacts[1] = cand_dedup[wolfe_idx[a]].contacts[1];
        test_cert.axes[a].contacts[2] = cand_dedup[wolfe_idx[a]].contacts[2];
      }
      TubeCertificate out_cert;
      std::string fail_reason;
      if (AuditCertificateAdaptive(tri, test_cert, &out_cert, &fail_reason)) {
        std::cout << AGREEN("  Balanced tet SUCCESS! c = ") << out_cert.c.ToString() << ", r = " << out_cert.r.ToString() << "\n";
        return;
      } else {
        std::cout << "  Balanced tet audit failed: " << fail_reason << "\n";
      }
    } else {
      std::cout << "  FindBalancedTetrahedron failed to find enclosing tet.\n";
    }

    // 3. Pool search
    std::set<int> pool_set;
    std::mt19937 rng(1337 + path.size() * 31);
    std::normal_distribution<double> gauss(0.0, 1.0);
    for (int d = 0; d < 256; d++) {
      vec3 dir = {gauss(rng), gauss(rng), gauss(rng)};
      int best_hi = 0, best_lo = 0;
      double max_v = yocto::dot(pts_dedup[0], dir);
      double min_v = max_v;
      for (size_t i = 1; i < pts_dedup.size(); i++) {
        double v = yocto::dot(pts_dedup[i], dir);
        if (v > max_v) { max_v = v; best_hi = i; }
        if (v < min_v) { min_v = v; best_lo = i; }
      }
      pool_set.insert(best_hi);
      pool_set.insert(best_lo);
    }
    std::vector<int> pool(pool_set.begin(), pool_set.end());
    std::cout << "  Pool size: " << pool.size() << " extreme points\n";

    struct ScoredTet {
      double margin;
      std::array<int, 4> indices;
    };
    std::vector<ScoredTet> scored;
    int P = pool.size();
    if (P >= 4) {
      uint64_t comb4 = (uint64_t)P * (P - 1) * (P - 2) * (P - 3) / 24;
      std::cout << "  Comb4: " << comb4 << "\n";
      if (comb4 <= 5000) {
        for (int i = 0; i < P; i++) {
          for (int j = i + 1; j < P; j++) {
            for (int k = j + 1; k < P; k++) {
              for (int l = k + 1; l < P; l++) {
                vec3 tpts[4] = {pts_dedup[pool[i]], pts_dedup[pool[j]], pts_dedup[pool[k]], pts_dedup[pool[l]]};
                double margin;
                double bary[4];
                if (TetrahedronOriginMargin(tpts, &margin, bary)) {
                  scored.push_back({margin, {pool[i], pool[j], pool[k], pool[l]}});
                }
              }
            }
          }
        }
      } else {
        for (int trial = 0; trial < 10000; trial++) {
          int i0 = pool[trial % P];
          std::uniform_int_distribution<int> dist(0, P - 1);
          int i1 = dist(rng), i2 = dist(rng), i3 = dist(rng);
          if (i0 == i1 || i0 == i2 || i0 == i3 || i1 == i2 || i1 == i3 || i2 == i3) continue;
          vec3 tpts[4] = {pts_dedup[i0], pts_dedup[pool[i1]], pts_dedup[pool[i2]], pts_dedup[pool[i3]]};
          double margin;
          double bary[4];
          if (TetrahedronOriginMargin(tpts, &margin, bary)) {
            scored.push_back({margin, {i0, pool[i1], pool[i2], pool[i3]}});
          }
        }
      }
    }
    std::cout << "  Scored enclosing tets: " << scored.size() << "\n";
    if (!scored.empty()) {
      std::sort(scored.begin(), scored.end(), [](const ScoredTet &a, const ScoredTet &b) {
        return a.margin > b.margin;
      });
      std::cout << "  Best tet margin: " << scored[0].margin << "\n";
      for (size_t idx = 0; idx < std::min((size_t)10, scored.size()); idx++) {
        TubeCertificate test_cert;
        for (int a = 0; a < 4; a++) {
          int c_idx = scored[idx].indices[a];
          test_cert.axes[a].contacts[0] = cand_dedup[c_idx].contacts[0];
          test_cert.axes[a].contacts[1] = cand_dedup[c_idx].contacts[1];
          test_cert.axes[a].contacts[2] = cand_dedup[c_idx].contacts[2];
        }
        TubeCertificate out_cert;
        std::string fail_reason;
        std::cout << "Trying scored tet " << idx << "...\n";
        for (int a = 0; a < 4; a++) {
          ContactInfo c[3] = {test_cert.axes[a].contacts[0], test_cert.axes[a].contacts[1], test_cert.axes[a].contacts[2]};
          Vec3Q center; BigRat delta; std::string rsn;
          bool ok = AuditAxis(tri, c, &out_cert.axes[a], &center, &delta, &rsn);
          std::cout << "  pre-check axis " << a << ": " << (ok ? "OK" : "FAIL") << " (" << rsn << ")"
                    << " [0]=(" << c[0].vertex << "," << c[0].edge_start << "->" << c[0].edge_finish << "," << c[0].edge_start2 << "->" << c[0].edge_finish2 << "," << c[0].mix << ")"
                    << " [1]=(" << c[1].vertex << "," << c[1].edge_start << "->" << c[1].edge_finish << "," << c[1].edge_start2 << "->" << c[1].edge_finish2 << "," << c[1].mix << ")"
                    << " [2]=(" << c[2].vertex << "," << c[2].edge_start << "->" << c[2].edge_finish << "," << c[2].edge_start2 << "->" << c[2].edge_finish2 << "," << c[2].mix << ")\n";
        }
        if (AuditCertificateAdaptive(tri, test_cert, &out_cert, &fail_reason)) {
          std::cout << AGREEN("  Scored tet SUCCESS! c = ") << out_cert.c.ToString() << ", r = " << out_cert.r.ToString() << ", delta = " << out_cert.delta.ToString() << "\n";
          for (int a = 0; a < 4; a++) {
            const auto &ax = out_cert.axes[a];
            std::cout << "  Axis " << a << " B=" << ax.B.ToString() << "\n";
            for (int m = 0; m < 3; m++) {
              std::cout << "    contact " << m << ": inner=" << ax.contacts[m].vertex
                        << " (" << ax.contacts[m].edge_start << "->" << ax.contacts[m].edge_finish << ") & ("
                        << ax.contacts[m].edge_start2 << "->" << ax.contacts[m].edge_finish2 << ") mix="
                        << ax.contacts[m].mix << " witness=" << ax.nonzero_witness[m] << "\n";
            }
          }
          return;
        } else if (idx == 0) {
          std::cout << "  Best scored tet audit fail: " << fail_reason << "\n";
        }
      }
    }
  }

  std::cout << "\nDiagnosis complete. Node failed to certify with current settings.\n";
}

// ============================================================================
// MAIN CLI
// ============================================================================

int main(int argc, char **argv) {
  std::string root_path = "0";
  int max_depth = 10;
  std::string target_r_str = "1/6000000";
  std::string target_c_str = "1018/10000000";
  int threads = 8;
  std::string output_dir = "../.artifacts/nopert229";
  bool revalidate = false;

  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--diagnose" && i + 1 < argc) {
      std::string path = argv[++i];
      DiagnosePath(path);
      return 0;
    } else if (arg == "--root_path" && i + 1 < argc) root_path = argv[++i];
    else if (arg == "--max_depth" && i + 1 < argc) max_depth = std::stoi(argv[++i]);
    else if (arg == "--target_r" && i + 1 < argc) target_r_str = argv[++i];
    else if (arg == "--target_c" && i + 1 < argc) target_c_str = argv[++i];
    else if (arg == "--threads" && i + 1 < argc) threads = std::stoi(argv[++i]);
    else if (arg == "--output_dir" && i + 1 < argc) output_dir = argv[++i];
    else if (arg == "--revalidate") revalidate = true;
  }

  BigRat target_r(target_r_str);
  BigRat target_c(target_c_str);

  IncrementalTubeManager manager(root_path, max_depth, target_r, target_c, threads, output_dir, revalidate);
  manager.Run();
  return 0;
}
