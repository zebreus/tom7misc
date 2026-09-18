#include "tube229.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "bignum/big.h"
#include "nopert229.h"
#include "tubetree229.h"
#include "util.h"
#include "base/stringprintf.h"
#include "yocto-math.h"

using namespace tubetree229;
using vec2 = yocto::vec<double, 2>;
using vec3 = yocto::vec<double, 3>;

// ============================================================================
// EXACT RATIONAL VERTICES OF NOPERT #229
// ============================================================================

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

Vec3Q Tube229::VertexQ(int v) {
  return Vec3Q(VERTICES_Q[v][0], VERTICES_Q[v][1], VERTICES_Q[v][2]);
}

vec3 Tube229::GetDoubleEdge(const ContactInfo &c) {
  vec3 v_start = Vertex(c.edge_start);
  vec3 v_finish = Vertex(c.edge_finish);
  vec3 v_start2 = Vertex(c.edge_start2);
  vec3 v_finish2 = Vertex(c.edge_finish2);
  double mixD = c.mix / 1000.0;
  return (v_start - v_finish) * mixD + (v_start2 - v_finish2) * (1.0 - mixD);
}

Vec3Q Tube229::GetExactEdge(const ContactInfo &c) {
  Vec3Q v_start(VERTICES_Q[c.edge_start][0], VERTICES_Q[c.edge_start][1], VERTICES_Q[c.edge_start][2]);
  Vec3Q v_finish(VERTICES_Q[c.edge_finish][0], VERTICES_Q[c.edge_finish][1], VERTICES_Q[c.edge_finish][2]);
  Vec3Q v_start2(VERTICES_Q[c.edge_start2][0], VERTICES_Q[c.edge_start2][1], VERTICES_Q[c.edge_start2][2]);
  Vec3Q v_finish2(VERTICES_Q[c.edge_finish2][0], VERTICES_Q[c.edge_finish2][1], VERTICES_Q[c.edge_finish2][2]);
  BigRat mixQ(c.mix, 1000);
  return (v_start - v_finish) * mixQ + (v_start2 - v_finish2) * (BigRat(1) - mixQ);
}

static inline double Cross2(const vec2 &a, const vec2 &b) {
  return a.x * b.y - a.y * b.x;
}

std::vector<int> Tube229::ConvexHull2D(const std::vector<vec2> &points) {
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

std::vector<ContactInfo> Tube229::GenerateSilhouetteContacts(
    const vec3 &view,
    const std::vector<int> &mix_samples_permille) {
  std::vector<ContactInfo> contacts;
  double vlen = yocto::length(view);
  if (vlen < 1e-12) return contacts;
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
  vec3 first = yocto::normalize(yocto::cross(unit_view, axis));
  vec3 second = yocto::cross(unit_view, first);

  std::vector<vec2> projected(NUM_VERTICES);
  for (int k = 0; k < NUM_VERTICES; k++) {
    projected[k] = vec2{yocto::dot(Vertex(k), first), yocto::dot(Vertex(k), second)};
  }
  std::vector<int> cycle = ConvexHull2D(projected);
  int H = cycle.size();

  for (int pos = 0; pos < H; pos++) {
    int v = cycle[pos];
    int prev = cycle[(pos - 1 + H) % H];
    int next = cycle[(pos + 1) % H];

    for (int mix : mix_samples_permille) {
      ContactInfo ci;
      ci.vertex = v;
      ci.edge_start = prev;
      ci.edge_finish = v;
      ci.edge_start2 = v;
      ci.edge_finish2 = next;
      ci.mix = mix;
      contacts.push_back(ci);
    }
  }
  return contacts;
}

std::vector<Tube229::CandidateTriple> Tube229::GenerateCandidateTriples(
    const TriangleQ &tri,
    const std::vector<ContactInfo> &contacts,
    double screen_support_error) {
  std::vector<CandidateTriple> out_candidates;
  vec3 tri_f[3] = {tri.corners[0].ToDouble(), tri.corners[1].ToDouble(), tri.corners[2].ToDouble()};
  vec3 centroid = (tri_f[0] + tri_f[1] + tri_f[2]) / 3.0;
  double vlen = yocto::length(centroid);
  if (vlen < 1e-12) return out_candidates;
  vec3 unit_view = centroid / vlen;

  struct ValidContact {
    ContactInfo ci;
    vec3 edge;
    vec3 lift;
    vec3 tau;
    double strict_slack;
  };

  std::vector<ValidContact> valid;
  for (const auto &c : contacts) {
    vec3 edge = GetDoubleEdge(c);
    int sel = c.vertex;
    bool ok = true;
    double strict_slack = 1e30;

    for (int k = 0; k < NUM_VERTICES; k++) {
      bool tie = (k == sel) ||
                 (c.mix == 1000 && sel == c.edge_finish && k == c.edge_start) ||
                 (c.mix == 0 && sel == c.edge_start2 && k == c.edge_finish2);
      if (tie) continue;
      vec3 delta = Vertex(k) - Vertex(sel);
      vec3 coeff = yocto::cross(edge, delta);
      double u0 = yocto::dot(tri_f[0], coeff);
      double u1 = yocto::dot(tri_f[1], coeff);
      double u2 = yocto::dot(tri_f[2], coeff);
      double max_u = std::max({u0, u1, u2});
      strict_slack = std::min(strict_slack, -max_u);
      if (max_u > screen_support_error) { ok = false; break; }
    }
    if (ok) {
      vec3 lift = yocto::cross(centroid, edge);
      vec3 tau = yocto::cross(Vertex(sel), lift);
      valid.push_back({c, edge, lift, tau, strict_slack});
    }
  }

  int V = valid.size();
  std::cout << "      [DEBUG] contacts input: " << contacts.size() << ", valid contacts: " << V << "\n";
  for (int i = 0; i < V; i++) {
    for (int j = i + 1; j < V; j++) {
      for (int k = j + 1; k < V; k++) {
        vec3 c_edges[3] = {valid[i].edge, valid[j].edge, valid[k].edge};
        vec3 lifts[3] = {valid[i].lift, valid[j].lift, valid[k].lift};

        vec3 w = {yocto::dot(unit_view, yocto::cross(lifts[1], lifts[2])),
                  yocto::dot(unit_view, yocto::cross(lifts[2], lifts[0])),
                  yocto::dot(unit_view, yocto::cross(lifts[0], lifts[1]))};
        if (w[0] <= 1e-12 && w[1] <= 1e-12 && w[2] <= 1e-12) w = -w;
        if (w[0] < -1e-10 || w[1] < -1e-10 || w[2] < -1e-10) continue;

        ContactInfo sel_contacts[3] = {valid[i].ci, valid[j].ci, valid[k].ci};
        int sel_indices[3] = {valid[i].ci.vertex, valid[j].ci.vertex, valid[k].ci.vertex};
        double sel_slack[3] = {valid[i].strict_slack, valid[j].strict_slack, valid[k].strict_slack};

        vec3 probe = {yocto::dot(tri_f[0], yocto::cross(c_edges[1], c_edges[2])),
                      yocto::dot(tri_f[0], yocto::cross(c_edges[2], c_edges[0])),
                      yocto::dot(tri_f[0], yocto::cross(c_edges[0], c_edges[1]))};
        if (std::max({probe[0], probe[1], probe[2]}) < 0) {
          std::swap(sel_contacts[1], sel_contacts[2]);
          std::swap(c_edges[1], c_edges[2]);
          std::swap(sel_indices[1], sel_indices[2]);
          std::swap(sel_slack[1], sel_slack[2]);
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
          double w_min = std::min({w0, w1, w2}) + screen_support_error;
          double w_max = std::max({w0, w1, w2}) + screen_support_error;
          if (w_min < 0) { weight_ok = false; break; }
          max_weight_lower = std::max(max_weight_lower, w_min);
          weights_at_max[m] = w_max;
        }
        if (!weight_ok || max_weight_lower <= 0) continue;

        double strict_slack = std::min({sel_slack[0], sel_slack[1], sel_slack[2]});

        vec3 weights = {yocto::dot(centroid, weight_coeffs[0]),
                        yocto::dot(centroid, weight_coeffs[1]),
                        yocto::dot(centroid, weight_coeffs[2])};
        double B = 2.0 * (weights_at_max[0] + weights_at_max[1] + weights_at_max[2]);
        if (B <= 1e-12) continue;

        vec3 variation = {0, 0, 0};
        for (int m = 0; m < 3; m++) {
          vec3 lift = yocto::cross(centroid, c_edges[m]);
          vec3 term = yocto::cross(Vertex(sel_indices[m]), lift);
          variation = variation + term * weights[m];
        }
        vec3 normalized_a = variation / B;

        CandidateTriple cand;
        cand.contacts[0] = sel_contacts[0];
        cand.contacts[1] = sel_contacts[1];
        cand.contacts[2] = sel_contacts[2];
        cand.normalized_a = normalized_a;
        cand.B = B;
        cand.strict_slack = strict_slack;
        out_candidates.push_back(cand);
      }
    }
  }
  return out_candidates;
}

std::vector<Tube229::CandidateTriple> Tube229::FindOpposingCandidates(
    const TriangleQ &tri,
    const vec3 &oppose_dir,
    int max_candidates,
    double screen_support_error) {
  double nlen = yocto::length(oppose_dir);
  if (nlen < 1e-12) return {};
  vec3 unit_n = oppose_dir / nlen;

  vec3 tri_f[3] = {tri.corners[0].ToDouble(), tri.corners[1].ToDouble(), tri.corners[2].ToDouble()};
  vec3 centroid = (tri_f[0] + tri_f[1] + tri_f[2]) / 3.0;

  // Use a fine mix permille grid to capture intermediate support edges
  std::vector<int> fine_mixes = {
    0, 1, 10, 25, 50, 75, 100, 150, 200, 250, 300, 350, 400, 450, 500,
    550, 600, 650, 700, 750, 800, 850, 900, 925, 950, 975, 990, 999, 1000
  };

  std::vector<ContactInfo> raw_contacts = GenerateSilhouetteContacts(centroid, fine_mixes);
  std::vector<CandidateTriple> all_cands = GenerateCandidateTriples(tri, raw_contacts, screen_support_error);
  double min_dot = 1e30;
  for (const auto &c : all_cands) {
    min_dot = std::min(min_dot, yocto::dot(c.normalized_a, unit_n));
  }
  std::cout << "    [DEBUG] raw_contacts: " << raw_contacts.size()
            << ", all_cands: " << all_cands.size()
            << ", min_dot: " << min_dot << "\n";

  // Score candidates by how strongly they oppose unit_n (maximizing a · (-unit_n), so a · unit_n < 0)
  struct ScoredTriple {
    double score;
    CandidateTriple triple;
  };

  std::vector<ScoredTriple> opposing;
  for (const auto &c : all_cands) {
    double dot = yocto::dot(c.normalized_a, unit_n);
    if (dot < 0.0) {
      opposing.push_back({-dot, c}); // score is positive magnitude opposing n
    }
  }

  std::sort(opposing.begin(), opposing.end(), [](const ScoredTriple &a, const ScoredTriple &b) {
    return a.score > b.score;
  });

  std::vector<CandidateTriple> result;
  int keep = std::min<int>(max_candidates, opposing.size());
  for (int i = 0; i < keep; i++) {
    result.push_back(opposing[i].triple);
  }
  return result;
}

bool Tube229::PointInTetrahedron(
    const vec3 &p, const vec3 &v0, const vec3 &v1, const vec3 &v2, const vec3 &v3,
    double *out_margin) {
  auto orient = [](const vec3 &a, const vec3 &b, const vec3 &c, const vec3 &d) {
    return yocto::dot(a - d, yocto::cross(b - d, c - d));
  };
  double d0 = orient(v0, v1, v2, p);
  double d1 = orient(v0, v2, v3, p);
  double d2 = orient(v0, v3, v1, p);
  double d3 = orient(v3, v2, v1, p);
  bool all_pos = (d0 > 1e-14 && d1 > 1e-14 && d2 > 1e-14 && d3 > 1e-14);
  bool all_neg = (d0 < -1e-14 && d1 < -1e-14 && d2 < -1e-14 && d3 < -1e-14);
  if (!all_pos && !all_neg) return false;

  if (out_margin) {
    auto dist_to_face = [](const vec3 &a, const vec3 &b, const vec3 &c) {
      vec3 n = yocto::cross(b - a, c - a);
      double len = yocto::length(n);
      if (len < 1e-14) return 0.0;
      return std::abs(yocto::dot(n, a)) / len;
    };
    *out_margin = std::min({
      dist_to_face(v0, v1, v2),
      dist_to_face(v0, v2, v3),
      dist_to_face(v0, v3, v1),
      dist_to_face(v1, v2, v3)
    });
  }
  return true;
}

struct ClosestResult {
  double key = 1e30;
  std::vector<int> support;
  vec3 point = {0, 0, 0};
};

static ClosestResult ClosestOriginPoint(const std::vector<vec3> &pts, const std::vector<int> &indices) {
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

bool Tube229::ClosestOriginFace(
    const std::vector<CandidateTriple> &candidates,
    const std::array<int, 4> &simplex,
    std::array<int, 3> *out_face,
    vec3 *out_normal) {
  std::vector<vec3> pts = {
    candidates[simplex[0]].normalized_a,
    candidates[simplex[1]].normalized_a,
    candidates[simplex[2]].normalized_a,
    candidates[simplex[3]].normalized_a
  };
  std::vector<int> indices = {0, 1, 2, 3};
  ClosestResult res = ClosestOriginPoint(pts, indices);
  if (res.support.size() == 3) {
    *out_face = {simplex[res.support[0]], simplex[res.support[1]], simplex[res.support[2]]};
    *out_normal = res.point;
    return true;
  }
  return false;
}

bool Tube229::FindBalancedTetrahedron(
    const std::vector<CandidateTriple> &candidates,
    std::array<int, 4> *out_simplex,
    vec3 *out_separating_normal) {
  int n = candidates.size();
  if (n < 4) return false;

  std::vector<vec3> pts(n);
  for (int i = 0; i < n; i++) pts[i] = candidates[i].normalized_a;

  int start = 0;
  double min_norm = yocto::dot(pts[0], pts[0]);
  for (int i = 1; i < n; i++) {
    double d = yocto::dot(pts[i], pts[i]);
    if (d < min_norm) { min_norm = d; start = i; }
  }

  std::vector<int> active = {start};
  std::set<std::pair<std::vector<int>, int>> seen;
  vec3 last_current = pts[start];

  for (int iter = 0; iter < 100; iter++) {
    if (active.size() >= 4) {
      int m = active.size();
      for (int i = 0; i < m; i++) {
        for (int j = i + 1; j < m; j++) {
          for (int k = j + 1; k < m; k++) {
            for (int l = k + 1; l < m; l++) {
              double margin = 0;
              if (PointInTetrahedron(vec3{0, 0, 0}, pts[active[i]], pts[active[j]], pts[active[k]], pts[active[l]], &margin) && margin > 1e-10) {
                *out_simplex = {active[i], active[j], active[k], active[l]};
                return true;
              }
            }
          }
        }
      }
    }

    ClosestResult closest = ClosestOriginPoint(pts, active);
    if (closest.support.empty()) {
      if (out_separating_normal) *out_separating_normal = last_current;
      return false;
    }
    active = closest.support;
    vec3 current = closest.point;
    last_current = current;
    double norm_sq = closest.key;

    int next_index = -1;
    double min_dot = 1e30;
    for (int i = 0; i < n; i++) {
      double d = yocto::dot(current, pts[i]);
      if (d < min_dot) { min_dot = d; next_index = i; }
    }
    double improvement = norm_sq - min_dot;
    if (improvement <= 1e-13 * std::max(1.0, norm_sq)) {
      if (out_separating_normal) *out_separating_normal = current;
      return false;
    }

    if (std::find(active.begin(), active.end(), next_index) != active.end()) {
      if (out_separating_normal) *out_separating_normal = current;
      return false;
    }

    std::vector<int> sorted_active = active;
    std::sort(sorted_active.begin(), sorted_active.end());
    if (seen.count({sorted_active, next_index})) {
      if (out_separating_normal) *out_separating_normal = current;
      return false;
    }
    seen.insert({sorted_active, next_index});
    active.push_back(next_index);
  }
  if (out_separating_normal) *out_separating_normal = last_current;
  return false;
}

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

BigRat Tube229::ExactTetrahedronAxisRadius(const Vec3Q pts[4]) {
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

bool Tube229::AuditAxis(const TriangleQ &tri, ContactInfo contacts[3], AxisCertificate *out_cert,
                        Vec3Q *out_center, BigRat *out_delta, std::string *fail_reason) {
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
    Vec3Q v_sel = VertexQ(sel);
    BigRat best_support(1000000);
    int best_w = -1;
    for (int k = 0; k < NUM_VERTICES; k++) {
      bool tie = (k == sel) ||
                 (contacts[m].mix == 1000 && sel == contacts[m].edge_finish && k == contacts[m].edge_start) ||
                 (contacts[m].mix == 0 && sel == contacts[m].edge_start2 && k == contacts[m].edge_finish2);
      BigRat s_upper(0);
      if (!tie) {
        Vec3Q v_k = VertexQ(k);
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
      Vec3Q v_supp = VertexQ(contacts[i].vertex);
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
  BigRat("1/50000000"), BigRat("1/100000000"),
  BigRat("1/200000000"), BigRat("1/500000000"),
  BigRat("1/1000000000"), BigRat("1/2000000000"),
  BigRat("1/5000000000"), BigRat("1/10000000000")
};

bool Tube229::AuditCertificateAdaptive(
    const TriangleQ &tri,
    const TubeCertificate &in_cert,
    TubeCertificate *out_cert,
    std::string *fail_reason) {

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
  BigRat diff = cover_radius - delta;
  if (diff <= BigRat(0)) {
    if (fail_reason) *fail_reason = StringPrintf("cover_radius - delta <= 0 (diff=%s, cover=%s, delta=%s)",
                                                 diff.ToString().c_str(), cover_radius.ToString().c_str(), delta.ToString().c_str());
    return false;
  }
  BigRat c = FloorTo(diff, 1000000000LL);
  if (c <= BigRat(0)) {
    c = FloorTo(diff, 1000000000000LL);
  }
  if (c <= BigRat(0)) {
    c = FloorTo(diff, 1000000000000000LL);
  }
  if (c <= BigRat(0)) {
    if (fail_reason) *fail_reason = StringPrintf("c <= 0 after high precision floor (diff=%s)",
                                                 diff.ToString().c_str());
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

bool Tube229::SynthesizeCertificate(
    const TriangleQ &tri,
    int depth,
    TubeCertificate *out_cert) {

  vec3 tri_f[3] = {tri.corners[0].ToDouble(), tri.corners[1].ToDouble(), tri.corners[2].ToDouble()};
  vec3 centroid = (tri_f[0] + tri_f[1] + tri_f[2]) / 3.0;

  // Pass 1: standard fast sampling with cone samples
  std::vector<int> std_mixes = {0, 1, 143, 286, 429, 571, 714, 857, 999, 1000};
  std::vector<ContactInfo> contacts = GenerateSilhouetteContacts(centroid, std_mixes);
  std::vector<CandidateTriple> candidates = GenerateCandidateTriples(tri, contacts, 1e-12);

  std::array<int, 4> simplex;
  vec3 oppose_dir = {0, 0, 0};
  if (FindBalancedTetrahedron(candidates, &simplex, &oppose_dir)) {
    TubeCertificate test_cert;
    for (int a = 0; a < 4; a++) {
      test_cert.axes[a].contacts[0] = candidates[simplex[a]].contacts[0];
      test_cert.axes[a].contacts[1] = candidates[simplex[a]].contacts[1];
      test_cert.axes[a].contacts[2] = candidates[simplex[a]].contacts[2];
    }
    if (AuditCertificateAdaptive(tri, test_cert, out_cert)) {
      return true;
    }
  }

  // Pass 2: Targeted Opposing Search
  if (yocto::length(oppose_dir) > 1e-12) {
    std::vector<CandidateTriple> opposing = FindOpposingCandidates(tri, oppose_dir, 50, 1e-12);
    candidates.insert(candidates.end(), opposing.begin(), opposing.end());

    if (FindBalancedTetrahedron(candidates, &simplex)) {
      TubeCertificate test_cert;
      for (int a = 0; a < 4; a++) {
        test_cert.axes[a].contacts[0] = candidates[simplex[a]].contacts[0];
        test_cert.axes[a].contacts[1] = candidates[simplex[a]].contacts[1];
        test_cert.axes[a].contacts[2] = candidates[simplex[a]].contacts[2];
      }
      if (AuditCertificateAdaptive(tri, test_cert, out_cert)) {
        return true;
      }
    }
  }

  return false;
}
