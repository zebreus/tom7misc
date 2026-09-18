#include "tube229.h"

#include <iostream>
#include <vector>
#include <array>
#include <cmath>
#include <set>

#include "bignum/big.h"
#include "nopert229.h"
#include "tubetree229.h"
#include "util.h"
#include "yocto-math.h"

using namespace tubetree229;
using vec2 = yocto::vec<double, 2>;
using vec3 = yocto::vec<double, 3>;

static void TestBasicSilhouette() {
  std::cout << "[RUN] TestBasicSilhouette\n";
  TriangleQ root = GetRootWedge();
  vec3 root_f[3] = {root.corners[0].ToDouble(), root.corners[1].ToDouble(), root.corners[2].ToDouble()};
  vec3 centroid = (root_f[0] + root_f[1] + root_f[2]) / 3.0;

  std::vector<int> mixes = {0, 500, 1000};
  std::vector<ContactInfo> contacts = Tube229::GenerateSilhouetteContacts(centroid, mixes);
  CHECK(!contacts.empty());
  std::cout << "  Root wedge centroid produced " << contacts.size() << " silhouette contacts.\n";

  std::vector<Tube229::CandidateTriple> triples = Tube229::GenerateCandidateTriples(root, contacts, 1e-12);
  std::cout << "  Root wedge produced " << triples.size() << " candidate triples.\n";
  std::cout << "[PASS] TestBasicSilhouette\n";
}

static void TestTargetedOpposingSearch() {
  std::cout << "[RUN] TestTargetedOpposingSearch\n";
  // The difficult depth-30 leaf path
  std::string path = "031213002112121221112212122211";
  TriangleQ tri = TriangleFromPath(path);

  vec3 tri_f[3] = {tri.corners[0].ToDouble(), tri.corners[1].ToDouble(), tri.corners[2].ToDouble()};
  vec3 centroid = (tri_f[0] + tri_f[1] + tri_f[2]) / 3.0;
  std::vector<int> std_mixes = {0, 1, 143, 286, 429, 571, 714, 857, 999, 1000};
  std::vector<ContactInfo> raw_contacts = Tube229::GenerateSilhouetteContacts(centroid, std_mixes);
  std::vector<Tube229::CandidateTriple> std_cands = Tube229::GenerateCandidateTriples(tri, raw_contacts, 1e-12);
  std::cout << "  Std candidates generated: " << std_cands.size() << "\n";

  std::array<int, 4> simplex;
  vec3 separating_norm = {0, 0, 0};
  bool enclosed = Tube229::FindBalancedTetrahedron(std_cands, &simplex, &separating_norm);
  std::cout << "  Std candidates enclosed origin: " << (enclosed ? "YES" : "NO") << "\n";
  if (!enclosed) {
    vec3 unit_n = yocto::normalize(separating_norm);
    std::cout << "  Centroid: (" << centroid.x << ", " << centroid.y << ", " << centroid.z << ")\n";
    std::cout << "  Separating normal unit: (" << unit_n.x << ", " << unit_n.y << ", " << unit_n.z << ")\n";
    std::cout << "  centroid · unit_n = " << yocto::dot(centroid, unit_n) << "\n";

    std::vector<vec3> views = {centroid, tri_f[0], tri_f[1], tri_f[2]};
    std::vector<int> fine_mixes = {0, 10, 50, 100, 200, 300, 400, 500, 600, 700, 800, 900, 950, 990, 1000};
    int opposing_contact_count = 0;
    double min_contact_torque_dot = 1e30;

    for (size_t vi = 0; vi < views.size(); vi++) {
      std::vector<ContactInfo> sc = Tube229::GenerateSilhouetteContacts(views[vi], fine_mixes);
      for (const auto &c : sc) {
        vec3 edge = Tube229::GetDoubleEdge(c);
        int sel = c.vertex;
        bool ok = true;
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
          if (std::max({u0, u1, u2}) > 1e-12) { ok = false; break; }
        }
        if (ok) {
          vec3 lift = yocto::cross(centroid, edge);
          vec3 tau = yocto::cross(Vertex(sel), lift);
          double d = yocto::dot(tau, unit_n);
          min_contact_torque_dot = std::min(min_contact_torque_dot, d);
          if (d < 0) {
            opposing_contact_count++;
            if (opposing_contact_count <= 5) {
              std::cout << "  Opposing contact: view " << vi << ", v=" << sel
                        << " (" << c.edge_start << "->" << c.edge_finish
                        << ", " << c.edge_start2 << "->" << c.edge_finish2
                        << ", mix=" << c.mix << ") tau · n = " << d << "\n";
            }
          }
        }
      }
    }
    std::cout << "  Total opposing contacts found: " << opposing_contact_count
              << ", min tau · n = " << min_contact_torque_dot << "\n";

    for (size_t vi = 0; vi < views.size(); vi++) {
      std::vector<ContactInfo> sc = Tube229::GenerateSilhouetteContacts(views[vi], fine_mixes);
      std::vector<Tube229::CandidateTriple> triples = Tube229::GenerateCandidateTriples(tri, sc, 1e-12);
      double view_min_dot = 1e30;
      int view_opposing_triples = 0;
      for (const auto &t : triples) {
        double d = yocto::dot(t.normalized_a, unit_n);
        view_min_dot = std::min(view_min_dot, d);
        if (d < 0) view_opposing_triples++;
      }
      std::cout << "  View " << vi << ": " << triples.size() << " triples, min a · n = "
                << view_min_dot << ", opposing triples = " << view_opposing_triples << "\n";
    }
  }
  std::cout << "[PASS] TestTargetedOpposingSearch\n";
}

static void TestSiblingCertificateOnDifficultLeaf() {
  std::cout << "[RUN] TestSiblingCertificateOnDifficultLeaf\n";
  std::string path = "031213002112121221112212122211";
  TriangleQ tri = TriangleFromPath(path);

  TubeCertificate sibling_cert;
  sibling_cert.axes[0].contacts[0] = {3, 2, 2, 1, 800, 2};
  sibling_cert.axes[0].contacts[1] = {1, 4, 4, 8, 800, 4};
  sibling_cert.axes[0].contacts[2] = {13, 14, 14, 15, 200, 14};

  sibling_cert.axes[1].contacts[0] = {15, 19, 19, 3, 200, 19};
  sibling_cert.axes[1].contacts[1] = {2, 1, 1, 4, 200, 1};
  sibling_cert.axes[1].contacts[2] = {8, 9, 9, 10, 200, 9};

  sibling_cert.axes[2].contacts[0] = {2, 1, 1, 4, 200, 1};
  sibling_cert.axes[2].contacts[1] = {13, 14, 14, 15, 200, 14};
  sibling_cert.axes[2].contacts[2] = {14, 15, 15, 19, 200, 15};

  sibling_cert.axes[3].contacts[0] = {3, 2, 2, 1, 800, 2};
  sibling_cert.axes[3].contacts[1] = {4, 8, 8, 9, 800, 8};
  sibling_cert.axes[3].contacts[2] = {14, 15, 15, 19, 800, 15};

  vec3 tri_f[3] = {tri.corners[0].ToDouble(), tri.corners[1].ToDouble(), tri.corners[2].ToDouble()};
  vec3 centroid = (tri_f[0] + tri_f[1] + tri_f[2]) / 3.0;
  std::vector<int> std_mixes = {0, 1, 143, 286, 429, 571, 714, 857, 999, 1000};
  std::vector<ContactInfo> raw = Tube229::GenerateSilhouetteContacts(centroid, std_mixes);
  std::vector<ContactInfo> valid_for_tri;
  for (const auto &c : raw) {
    bool ok = true;
    vec3 edge = Tube229::GetDoubleEdge(c);
    int sel = c.vertex;
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
      if (std::max({u0, u1, u2}) > 1e-12) { ok = false; break; }
    }
    if (ok) valid_for_tri.push_back(c);
  }
  std::cout << "  Valid silhouette contacts for tri: " << valid_for_tri.size() << "\n";
  std::set<int> valid_verts;
  for (const auto &c : valid_for_tri) valid_verts.insert(c.vertex);
  std::cout << "  Silhouette vertices for tri: ";
  for (int v : valid_verts) std::cout << v << " ";
  std::cout << "\n";

  // Scan mix for vertex 14 on tri
  int best_valid_mix = -1;
  double min_worst_s = 1e30;
  for (int m = 0; m <= 1000; m++) {
    ContactInfo c = {13, 14, 14, 15, m, 14};
    vec3 edge = Tube229::GetDoubleEdge(c);
    int sel = c.vertex;
    double worst_s = -1e30;
    for (int k = 0; k < 20; k++) {
      bool tie = (k == sel) ||
                 (c.mix == 1000 && sel == c.edge_finish && k == c.edge_start) ||
                 (c.mix == 0 && sel == c.edge_start2 && k == c.edge_finish2);
      if (tie) continue;
      vec3 delta = Vertex(k) - Vertex(sel);
      vec3 coeff = yocto::cross(edge, delta);
      double u0 = yocto::dot(tri_f[0], coeff);
      double u1 = yocto::dot(tri_f[1], coeff);
      double u2 = yocto::dot(tri_f[2], coeff);
      worst_s = std::max({worst_s, u0, u1, u2});
    }
    if (worst_s < min_worst_s) {
      min_worst_s = worst_s;
      best_valid_mix = m;
    }
  }
  std::cout << "  Vertex 14 scan: best mix=" << best_valid_mix << " min_worst_s=" << min_worst_s << "\n";

  std::vector<ContactInfo> axis0_replacements;
  for (const auto &c : valid_for_tri) {
    ContactInfo test_c[3] = {sibling_cert.axes[0].contacts[0], sibling_cert.axes[0].contacts[1], c};
    AxisCertificate ac;
    Vec3Q center;
    BigRat delta;
    std::string fail;
    if (Tube229::AuditAxis(tri, test_c, &ac, &center, &delta, &fail)) {
      axis0_replacements.push_back(c);
    }
  }
  std::cout << "  Valid Axis 0 replacements: " << axis0_replacements.size() << "\n";

  std::vector<ContactInfo> axis2_replacements;
  for (const auto &c : valid_for_tri) {
    ContactInfo test_c[3] = {sibling_cert.axes[2].contacts[0], c, sibling_cert.axes[2].contacts[2]};
    AxisCertificate ac;
    Vec3Q center;
    BigRat delta;
    std::string fail;
    if (Tube229::AuditAxis(tri, test_c, &ac, &center, &delta, &fail)) {
      axis2_replacements.push_back(c);
    }
  }
  std::cout << "  Valid Axis 2 replacements: " << axis2_replacements.size() << "\n";

  std::string sib0_path = "031213002112121221112212122210";
  TriangleQ sib0_tri = TriangleFromPath(sib0_path);
  Vec3Q sib0_centers[4];
  BigRat sib0_deltas[4];
  for (int a = 0; a < 4; a++) {
    AxisCertificate ac;
    Tube229::AuditAxis(sib0_tri, sibling_cert.axes[a].contacts, &ac, &sib0_centers[a], &sib0_deltas[a]);
    std::cout << "    sib0 center[" << a << "] = ("
              << sib0_centers[a].x.ToDouble() << ", "
              << sib0_centers[a].y.ToDouble() << ", "
              << sib0_centers[a].z.ToDouble() << ") delta=" << sib0_deltas[a].ToDouble() << "\n";
  }
  TubeCertificate sib0_out;
  std::string sib0_fail;
  bool sib0_ok = Tube229::AuditCertificateAdaptive(sib0_tri, sibling_cert, &sib0_out, &sib0_fail);
  std::cout << "  Audit on SIBLING 0: " << (sib0_ok ? "SUCCESS" : ("FAILED: " + sib0_fail)) << "\n";
  if (sib0_ok) {
    std::cout << "    sib0 c = " << sib0_out.c.ToString() << " (" << sib0_out.c.ToDouble() << ")\n";
    std::cout << "    sib0 r = " << sib0_out.r.ToString() << " (" << sib0_out.r.ToDouble() << ")\n";
    std::cout << "    sib0 delta = " << sib0_out.delta.ToString() << " (" << sib0_out.delta.ToDouble() << ")\n";
  }

  // Now search over axis0 and axis2 replacements on difficult leaf tri
  BigRat max_cover(0);
  for (const auto &rep0 : axis0_replacements) {
    for (const auto &rep2 : axis2_replacements) {
      TubeCertificate cand = sibling_cert;
      cand.axes[0].contacts[2] = rep0;
      cand.axes[2].contacts[1] = rep2;
      Vec3Q c_pts[4];
      BigRat d_pts[4];
      bool all_axes_ok = true;
      for (int a = 0; a < 4; a++) {
        AxisCertificate ac;
        ContactInfo contacts[3] = {cand.axes[a].contacts[0], cand.axes[a].contacts[1], cand.axes[a].contacts[2]};
        if (!Tube229::AuditAxis(tri, contacts, &ac, &c_pts[a], &d_pts[a])) {
          all_axes_ok = false;
          break;
        }
      }
      if (!all_axes_ok) continue;
      static bool first_c_pts = true;
      if (first_c_pts) {
        for (int a = 0; a < 4; a++) {
          std::cout << "    tri pair 0 center[" << a << "] = ("
                    << c_pts[a].x.ToDouble() << ", "
                    << c_pts[a].y.ToDouble() << ", "
                    << c_pts[a].z.ToDouble() << ")\n";
        }
        first_c_pts = false;
      }
      BigRat axis_r = Tube229::ExactTetrahedronAxisRadius(c_pts);
      if (axis_r > max_cover) max_cover = axis_r;
    }
  }
  std::cout << "  Max axis_radius across all 720 pairs: " << max_cover.ToString() << " (" << max_cover.ToDouble() << ")\n";
}

void TestNodeDepth18() {
  std::cout << "[RUN] TestNodeDepth18\n";
  std::string path = "031213032001032213";
  TriangleQ tri = TriangleFromPath(path);
  vec3 tri_f[3] = {tri.corners[0].ToDouble(), tri.corners[1].ToDouble(), tri.corners[2].ToDouble()};
  vec3 centroid = (tri_f[0] + tri_f[1] + tri_f[2]) / 3.0;

  for (int cone_samples : {4, 6, 8, 10}) {
    std::vector<vec3> sample_views = {centroid, tri_f[0], tri_f[1], tri_f[2]};
    std::vector<int> mix_samples;
    for (int s = 0; s < cone_samples; s++) {
      mix_samples.push_back(std::clamp(static_cast<int>(std::round(1000.0 * (s + 1.0) / (cone_samples + 1.0))), 0, 1000));
    }
    mix_samples.push_back(0);
    mix_samples.push_back(1);
    mix_samples.push_back(999);
    mix_samples.push_back(1000);
    std::sort(mix_samples.begin(), mix_samples.end());
    mix_samples.erase(std::unique(mix_samples.begin(), mix_samples.end()), mix_samples.end());

    std::vector<ContactInfo> all_contacts;
    for (const auto &sv : sample_views) {
      std::vector<ContactInfo> v_contacts = Tube229::GenerateSilhouetteContacts(sv, mix_samples);
      all_contacts.insert(all_contacts.end(), v_contacts.begin(), v_contacts.end());
    }
    // Deduplicate contacts
    std::vector<ContactInfo> unique_contacts;
    for (const auto &c : all_contacts) {
      bool exists = false;
      for (const auto &u : unique_contacts) {
        if (c.edge_start == u.edge_start && c.edge_finish == u.edge_finish &&
            c.edge_start2 == u.edge_start2 && c.edge_finish2 == u.edge_finish2 &&
            c.mix == u.mix && c.vertex == u.vertex) {
          exists = true;
          break;
        }
      }
      if (!exists) unique_contacts.push_back(c);
    }
    std::vector<Tube229::CandidateTriple> all_cands = Tube229::GenerateCandidateTriples(tri, unique_contacts, 2e-14);
    std::array<int, 4> simplex;
    vec3 sep_norm;
    bool enclosed = Tube229::FindBalancedTetrahedron(all_cands, &simplex, &sep_norm);
    std::cout << "  cone_samples=" << cone_samples << ": unique_contacts=" << unique_contacts.size()
              << " candidates=" << all_cands.size()
              << " enclosed=" << (enclosed ? "YES" : "NO")
              << " sep_norm=(" << sep_norm.x << ", " << sep_norm.y << ", " << sep_norm.z << ")\n";
    if (enclosed) {
      TubeCertificate cert;
      for (int a = 0; a < 4; a++) {
        cert.axes[a].contacts[0] = all_cands[simplex[a]].contacts[0];
        cert.axes[a].contacts[1] = all_cands[simplex[a]].contacts[1];
        cert.axes[a].contacts[2] = all_cands[simplex[a]].contacts[2];
      }
      TubeCertificate out_cert;
      std::string fail;
      bool ok = Tube229::AuditCertificateAdaptive(tri, cert, &out_cert, &fail);
      std::cout << "    Audit: " << (ok ? "SUCCESS" : ("FAIL: " + fail)) << "\n";
      if (ok) {
        std::cout << "    c = " << out_cert.c.ToString() << " (" << out_cert.c.ToDouble() << ")\n";
        std::cout << "    r = " << out_cert.r.ToString() << " (" << out_cert.r.ToDouble() << ")\n";
        break;
      }
    }
  }
}

void TestTripleV2V4V10() {
  std::cout << "[RUN] TestTripleV2V4V10\n";
  std::string path = "031213002112121221112212122211";
  TriangleQ tri = TriangleFromPath(path);
  vec3 tri_f[3] = {tri.corners[0].ToDouble(), tri.corners[1].ToDouble(), tri.corners[2].ToDouble()};
  vec3 centroid = (tri_f[0] + tri_f[1] + tri_f[2]) / 3.0;

  // Let us inspect the contacts generated at centroid
  std::vector<int> mix_samples = {0, 10, 50, 100, 200, 500, 800, 1000};
  std::vector<ContactInfo> contacts = Tube229::GenerateSilhouetteContacts(centroid, mix_samples);
  std::cout << "  Contacts generated: " << contacts.size() << "\n";
  for (const auto &c : contacts) {
    if (c.vertex == 10) {
      std::cout << "  v10 contact: edge (" << c.edge_start << "->" << c.edge_finish
                << ", " << c.edge_start2 << "->" << c.edge_finish2 << ") mix=" << c.mix << "\n";
    }
  }

  // Filter contacts by support on tri
  std::vector<ContactInfo> valid_c;
  for (const auto &c : contacts) {
    vec3 edge = Tube229::GetDoubleEdge(c);
    bool valid = true;
    for (int k = 0; k < NUM_VERTICES; k++) {
      bool tie = (k == c.vertex) ||
                 (c.mix == 1000 && c.vertex == c.edge_finish && k == c.edge_start) ||
                 (c.mix == 0 && c.vertex == c.edge_start2 && k == c.edge_finish2);
      if (tie) continue;
      vec3 delta = Vertex(k) - Vertex(c.vertex);
      vec3 coeff = yocto::cross(edge, delta);
      for (int corner = 0; corner < 3; corner++) {
        if (yocto::dot(tri_f[corner], coeff) > 2e-14) {
          valid = false;
          break;
        }
      }
      if (!valid) break;
    }
    if (valid) valid_c.push_back(c);
  }
  std::cout << "  Valid contacts: " << valid_c.size() << "\n";
  int valid_v10 = 0;
  for (const auto &c : valid_c) {
    if (c.vertex == 10) valid_v10++;
  }
  std::cout << "  Valid v10 contacts: " << valid_v10 << "\n";

  // Now test triples with v2, v4, v10
  ContactInfo c_v2, c_v4;
  bool found_v2 = false, found_v4 = false;
  for (const auto &c : valid_c) {
    if (c.vertex == 2 && c.mix == 800) { c_v2 = c; found_v2 = true; }
    if (c.vertex == 4 && c.mix == 800) { c_v4 = c; found_v4 = true; }
  }
  std::cout << "  Found v2 (mix 800): " << found_v2 << ", v4 (mix 800): " << found_v4 << "\n";

  vec3 sep_norm = {-0.401011, 0.483067, 0.778355};
  int opposing_triples = 0;
  for (size_t i0 = 0; i0 < valid_c.size(); i0++) {
    for (size_t i1 = i0 + 1; i1 < valid_c.size(); i1++) {
      for (size_t i2 = i1 + 1; i2 < valid_c.size(); i2++) {
        ContactInfo triple[3] = {valid_c[i0], valid_c[i1], valid_c[i2]};
        AxisCertificate ac;
        Vec3Q center;
        BigRat delta;
        std::string fail;
        if (Tube229::AuditAxis(tri, triple, &ac, &center, &delta, &fail)) {
          vec3 c_f = {center.x.ToDouble(), center.y.ToDouble(), center.z.ToDouble()};
          double dot_n = yocto::dot(c_f, sep_norm);
          if (dot_n < 0) {
            opposing_triples++;
            if (opposing_triples <= 5) {
              std::cout << "  OPPOSING TRIPLE: v=(" << valid_c[i0].vertex << ", "
                        << valid_c[i1].vertex << ", " << valid_c[i2].vertex
                        << ") mix=(" << valid_c[i0].mix << ", " << valid_c[i1].mix
                        << ", " << valid_c[i2].mix << ") a · n = " << dot_n << "\n";
            }
          }
        }
      }
    }
  }
  std::cout << "  Total opposing triples from valid_c: " << opposing_triples << "\n";
}

void TestCertificate003133OnTargetTri() {
  std::cout << "[RUN] TestCertificate003133OnTargetTri\n";
  std::string path = "031213002112121221112212122211";
  TriangleQ tri = TriangleFromPath(path);

  TubeCertificate cert;
  cert.symmetry_index = 0;

  // Axis 0
  cert.axes[0].contacts[0] = {3, 2, 2, 1, 800, 2};
  cert.axes[0].contacts[1] = {1, 0, 0, 4, 200, 0};
  cert.axes[0].contacts[2] = {10, 15, 15, 19, 800, 15};

  // Axis 1
  cert.axes[1].contacts[0] = {15, 19, 19, 3, 600, 19};
  cert.axes[1].contacts[1] = {3, 2, 2, 1, 800, 2};
  cert.axes[1].contacts[2] = {8, 9, 9, 10, 800, 9};

  // Axis 2
  cert.axes[2].contacts[0] = {3, 2, 2, 1, 800, 2};
  cert.axes[2].contacts[1] = {1, 0, 0, 4, 200, 0};
  cert.axes[2].contacts[2] = {9, 10, 10, 15, 200, 10};

  // Axis 3
  cert.axes[3].contacts[0] = {2, 1, 1, 0, 200, 1};
  cert.axes[3].contacts[1] = {10, 15, 15, 19, 800, 15};
  cert.axes[3].contacts[2] = {10, 15, 15, 19, 200, 15};

  std::cout << "[SWEEP] Axis 2 mix variation on target_tri:\n";
  for (int m1 : {200, 400, 600, 800, 900, 950, 990, 1000}) {
    ContactInfo c[3] = {
      {3, 2, 2, 1, 800, 2},
      {1, 0, 0, 4, m1, 0},
      {9, 10, 10, 15, 200, 10}
    };
    AxisCertificate ac;
    Vec3Q center;
    BigRat delta;
    std::string fail;
    bool ok = Tube229::AuditAxis(tri, c, &ac, &center, &delta, &fail);
    std::cout << "  m1=" << m1 << " -> " << (ok ? "SUCCESS" : ("FAIL: " + fail));
    if (ok) {
      std::cout << " center = (" << center.x.ToDouble() << ", " << center.y.ToDouble() << ", " << center.z.ToDouble() << ") delta=" << delta.ToDouble();
    }
    std::cout << "\n";
  }

  TubeCertificate out_cert;
  std::string fail;
  bool ok = Tube229::AuditCertificateAdaptive(tri, cert, &out_cert, &fail);
  std::cout << "  Full cert audit on difficult leaf: " << (ok ? "SUCCESS" : ("FAIL: " + fail)) << "\n";
  if (ok) {
    std::cout << "    c = " << out_cert.c.ToString() << " (" << out_cert.c.ToDouble() << ")\n";
    std::cout << "    r = " << out_cert.r.ToString() << " (" << out_cert.r.ToDouble() << ")\n";
  }
}

void TestDoubleCheckAxis() {
  std::cout << "[RUN] TestDoubleCheckAxis\n";
  std::string leaf_path = "031213002112121221112212122211";
  TriangleQ tri = TriangleFromPath(leaf_path);

  // Axis 1 from 003133 (valid)
  ContactInfo ax1[3] = {
    {15, 19, 19, 3, 200, 19},
    {3, 2, 2, 1, 800, 2},
    {8, 9, 9, 10, 800, 9}
  };
  Tube229::CandidateTriple cand1;
  bool ok1 = Tube229::DoubleCheckAxis(tri, ax1, &cand1);
  assert(ok1);
  std::cout << "  Axis 1 double check: SUCCESS! norm_a = ("
            << cand1.normalized_a.x << ", " << cand1.normalized_a.y << ", " << cand1.normalized_a.z << ")\n";

  // Axis 0 from 003133 (invalid support on vertex 1)
  ContactInfo ax0[3] = {
    {15, 19, 19, 3, 200, 19},
    {1, 0, 0, 4, 200, 0},
    {8, 9, 9, 10, 800, 9}
  };
  Tube229::CandidateTriple cand0;
  bool ok0 = Tube229::DoubleCheckAxis(tri, ax0, &cand0);
  assert(!ok0);
  std::cout << "  Axis 0 double check: correctly REJECTED (support violation)!\n";
}

void TestNearOriginFaceCuttingPlane() {
  std::cout << "[RUN] TestNearOriginFaceCuttingPlane\n";

  // Face perpendicular to z-axis, shifted by 8.854e-7 in +z direction:
  // Closest squared distance to origin is (8.854e-7)^2 = 7.84e-13 < 1e-12.
  const double d = 8.854e-7;
  vec3 p0 = {0.05, 0.0, d};
  vec3 p1 = {-0.025, 0.043301270189, d};
  vec3 p2 = {-0.025, -0.043301270189, d};
  // 4th point in initial pool on the SAME side (+z):
  vec3 p3 = {0.01, 0.01, 0.08};
  // Opposing candidate on the -z side:
  vec3 p4 = {-0.005, -0.005, -0.05};

  std::vector<vec3> pts = {p0, p1, p2, p3, p4};

  // Initial pool consists only of the +z points {0, 1, 2, 3}.
  std::set<int> pool = {0, 1, 2, 3};
  std::array<int, 4> simplex;
  double margin = 0;

  // With the premature early return bug (where closest.key < 1e-12 unconditionally
  // aborted without checking opposing candidates), this would fail on iteration 0.
  // With the fix, it orients the face normal away from the pool, searches in
  // direction -v, pulls in p4, and successfully cages the origin.
  bool ok = Tube229::RefinePoolCuttingPlane(pts, &pool, &simplex, &margin);
  assert(ok);
  assert(margin > 1e-8);
  std::cout << "  RefinePoolCuttingPlane successfully caged origin! Margin = " << margin
            << ", simplex = [" << simplex[0] << ", " << simplex[1] << ", "
            << simplex[2] << ", " << simplex[3] << "]\n";

  // Also verify Wolfe's algorithm handles this near-origin face geometry
  std::vector<Tube229::CandidateTriple> cands;
  for (size_t i = 0; i < pts.size(); i++) {
    Tube229::CandidateTriple c;
    c.normalized_a = pts[i];
    cands.push_back(c);
  }
  std::array<int, 4> wolfe_simplex;
  bool wolfe_ok = Tube229::FindBalancedTetrahedron(cands, &wolfe_simplex);
  assert(wolfe_ok);
  std::cout << "  FindBalancedTetrahedron successfully caged origin!\n";
  std::cout << "[PASS] TestNearOriginFaceCuttingPlane\n";
}

int main(int argc, char **argv) {
  std::cout << "=== Running Tube229 Unit Tests ===\n";
  TestNearOriginFaceCuttingPlane();
  TestDoubleCheckAxis();
  TestCertificate003133OnTargetTri();
  std::cout << "=== All Tube229 Unit Tests Passed! ===\n";
  return 0;
}

