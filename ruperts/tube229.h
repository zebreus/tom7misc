#ifndef _RUPERTS_TUBE229_H
#define _RUPERTS_TUBE229_H

#include <vector>
#include <string>
#include <string_view>
#include <array>
#include <utility>

#include "nopert229.h"
#include "tubetree229.h"
#include "yocto-math.h"
#include "bignum/big.h"

// Collection of geometric, optimization, and exact verification routines
// for 4-axis identity tube certificates of Nopert #229.
class Tube229 {
 public:
  using vec2 = yocto::vec<double, 2>;
  using vec3 = yocto::vec<double, 3>;

  struct CandidateTriple {
    tubetree229::ContactInfo contacts[3];
    vec3 normalized_a;
    double strict_slack = 0.0;
    double B = 0.0;
  };

  // Exact rational vertex access for Nopert #229 (0..19).
  static tubetree229::Vec3Q VertexQ(int v);

  // Approximate double edge vector for a contact.
  static vec3 GetDoubleEdge(const tubetree229::ContactInfo &c);

  // Exact rational edge vector for a contact.
  static tubetree229::Vec3Q GetExactEdge(const tubetree229::ContactInfo &c);

  // 2D convex hull of projected silhouette points.
  static std::vector<int> ConvexHull2D(const std::vector<vec2> &points);

  // Generates silhouette boundary contacts for a given view direction with specified mix values.
  static std::vector<tubetree229::ContactInfo> GenerateSilhouetteContacts(
      const vec3 &view,
      const std::vector<int> &mix_samples_permille);

  // Generates valid 3-contact CandidateTriples over a triangle with configurable screening tolerance.
  static std::vector<CandidateTriple> GenerateCandidateTriples(
      const tubetree229::TriangleQ &tri,
      const std::vector<tubetree229::ContactInfo> &contacts,
      double screen_support_error = 1e-12);

  // Targeted Opposing Search (Tom Idea Point 4):
  // Given an unblocked direction n, specifically finds candidate triples with a · n < 0 (maximizing a · (-n)).
  static std::vector<CandidateTriple> FindOpposingCandidates(
      const tubetree229::TriangleQ &tri,
      const vec3 &oppose_dir,
      int max_candidates = 50,
      double screen_support_error = 1e-12);

  // Simplex geometry checks
  static bool PointInTetrahedron(
      const vec3 &p, const vec3 &v0, const vec3 &v1, const vec3 &v2, const vec3 &v3,
      double *out_margin = nullptr);

  // Finds closest face of simplex to origin and returns outward normal.
  static bool ClosestOriginFace(
      const std::vector<CandidateTriple> &candidates,
      const std::array<int, 4> &simplex,
      std::array<int, 3> *out_face,
      vec3 *out_normal);

  // Wolfe's minimum norm point / balanced tetrahedron algorithm.
  // If origin is not enclosed, returns false and optionally writes separating normal to out_separating_normal.
  static bool FindBalancedTetrahedron(
      const std::vector<CandidateTriple> &candidates,
      std::array<int, 4> *out_simplex,
      vec3 *out_separating_normal = nullptr);

  // Exact rational inscribed octahedron / axis radius.
  static BigRat ExactTetrahedronAxisRadius(const tubetree229::Vec3Q pts[4]);

  // Audits a single 3-contact axis over triangle tri in exact rational arithmetic.
  static bool AuditAxis(
      const tubetree229::TriangleQ &tri,
      tubetree229::ContactInfo contacts[3],
      tubetree229::AxisCertificate *out_cert,
      tubetree229::Vec3Q *out_center,
      BigRat *out_delta,
      std::string *fail_reason = nullptr);

  // Exact adaptive rational audit of a 4-axis cage.
  static bool AuditCertificateAdaptive(
      const tubetree229::TriangleQ &tri,
      const tubetree229::TubeCertificate &test_cert,
      tubetree229::TubeCertificate *out_cert,
      std::string *fail_reason = nullptr);

  // Synthesize a valid tube certificate for triangle tri, with fallback to opposing search.
  static bool SynthesizeCertificate(
      const tubetree229::TriangleQ &tri,
      int depth,
      tubetree229::TubeCertificate *out_cert);
};

#endif  // _RUPERTS_TUBE229_H
