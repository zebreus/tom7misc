
#ifndef _ALBRECHTFAST_ALBRECHT_H
#define _ALBRECHTFAST_ALBRECHT_H

#include <cmath>
#include <limits>
#include <optional>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

#include "bit-string.h"
#include "geom/polyhedra.h"
#include "hashing.h"
#include "inline-vector.h"
#include "svg.h"
#include "yocto-math.h"

// Some code for investigating Albrecht Dürer's conjecture that every
// convex polyhedron has a net.
//
// Given a polyhedron (which has its faces and vertices numbered), we
// can represent a net as a tree. Each node corresponds to a face.
// For a face with n edges, its node has n children; each child is
// either empty (the edge is not unfolded) or a node containing
// that adjacent face, unfolded.
//
// Since the order of the children indicates the identity of the
// faces (an edge connects the current face to one other face), the
// children can be represented as simply a bit string. And since
// every face appears in the net, we can also canonically represent
// the entire net as a bit string: The 0th face is first, containing
// a single bit for each of its children (in order). Then we have a
// depth-first expansion of each child, in order, that is 1.
//
// An even simpler way to think about this is that we order all
// of the edges in the polyhedron, and then a net can just be
// represented as the set of all edges that are unfolded.

struct Albrecht {

  // Precomputations for a polyhedron that may make testing nets,
  // (or searching for them, etc.) more efficient.
  struct AugmentedPoly {
    Polyhedron poly;

    // For each face (in the same order as in the poly), all of
    // its edge indices. The edges appear in the same order that
    // they do in faces->v (i.e., the first edge is from v[0] to v[1])
    // but note that since the same edge appears in two faces with
    // opposite directions, Edge::v0 is not necessarily this face's
    // starting vertex.
    std::vector<InlineVector<int>> face_edges;

    // For each face (in the same order as in the poly), its 2D
    // polygon on the XY plane. A canonical pose:
    //   - Vertices are the same order as they appear in the face
    //   - The first vertex is at 0,0.
    //   - The first edge is vertical.
    // The polygon will have Cartesian CW (screen CCW) winding order.
    std::vector<std::vector<vec2>> polygons;

    // For each edge, two transformations. The first maps face1's
    // canonical 2D pose to face0's, such that the edge overlaps.
    // The second maps face0's to face1's.
    std::vector<std::pair<frame2, frame2>> edge_transforms;


    // Compute the augmented information for the polyhedron up front.
    AugmentedPoly(Polyhedron poly_in) : poly(std::move(poly_in)) {

      polygons.reserve(poly.faces->v.size());
      for (const std::vector<int> &face : poly.faces->v) {
        std::vector<vec2> poly2d;
        poly2d.reserve(face.size());

        CHECK(face.size() >= 3) << "Invalid face.";
        const vec3 &v0 = poly.vertices[face[0]];
        const vec3 &v1 = poly.vertices[face[1]];
        const vec3 &v2 = poly.vertices[face[2]];

        // 2D coordinate system for the face, placing the first
        // edge vertical.
        const vec3 ey = normalize(v1 - v0);
        const vec3 n = normalize(cross(ey, v2 - v0));
        const vec3 ex = cross(n, ey);

        for (int idx : face) {
          vec3 p = poly.vertices[idx] - v0;
          poly2d.push_back(vec2{dot(p, ex), dot(p, ey)});
        }

        polygons.push_back(std::move(poly2d));
      }

      // Compute face_edges.
      std::unordered_map<std::pair<int, int>, int,
                         Hashing<std::pair<int, int>>> edge_map;
      for (int e = 0; e < poly.faces->NumEdges(); ++e) {
        auto k1 = std::make_pair(poly.faces->edges[e].v0,
                                 poly.faces->edges[e].v1);
        auto k2 = std::make_pair(poly.faces->edges[e].v1,
                                 poly.faces->edges[e].v0);
        edge_map[k1] = e;
        edge_map[k2] = e;
      }

      face_edges.resize(poly.faces->NumFaces());
      for (int f = 0; f < poly.faces->NumFaces(); ++f) {
        const std::vector<int> &face = poly.faces->v[f];
        for (size_t i = 0; i < face.size(); ++i) {
          int v0 = face[i];
          int v1 = face[(i + 1) % face.size()];
          auto it = edge_map.find(std::make_pair(v0, v1));
          CHECK(it != edge_map.end()) << "Edge not found!";
          face_edges[f].push_back(it->second);
        }
      }

      // Compute edge transforms.
      edge_transforms.reserve(poly.faces->edges.size());
      for (const Faces::Edge &edge : poly.faces->edges) {
        // Get the index of the vertex within the face.
        auto FindInFace = [](std::span<const int> face, int v) {
          for (int i = 0; i < (int)face.size(); ++i) {
            if (face[i] == v) return i;
          }
          LOG(FATAL) << "Invalid poly: " << v << " not in face!";
          return -1;
        };

        // The 2d points in face 0.
        vec2 p0 = polygons[edge.f0][
            FindInFace(poly.faces->v[edge.f0], edge.v0)];
        vec2 p1 = polygons[edge.f0][
            FindInFace(poly.faces->v[edge.f0], edge.v1)];

        vec2 q0 = polygons[edge.f1][
            FindInFace(poly.faces->v[edge.f1], edge.v0)];
        vec2 q1 = polygons[edge.f1][
            FindInFace(poly.faces->v[edge.f1], edge.v1)];

        // Compute rigid transform mapping (src0, src1) to (dst0, dst1)
        auto Transform = [](vec2 src0, vec2 src1, vec2 dst0, vec2 dst1) {
          vec2 v_src = src1 - src0;
          vec2 v_dst = dst1 - dst0;
          double angle = std::atan2(v_dst.y, v_dst.x) -
            std::atan2(v_src.y, v_src.x);
          frame2 f = rotation_frame2(angle);
          f.o = dst0 - yocto::transform_point(f, src0);
          return f;
        };

        frame2 f1_to_f0 = Transform(q0, q1, p0, p1);
        frame2 f0_to_f1 = Transform(p0, p1, q0, q1);

        edge_transforms.push_back({f1_to_f0, f0_to_f1});
      }
    }
  };

  struct PlacedFace {
    int face_idx = 0;
    // In the same order they appear in the Polyhedron's face.
    std::vector<vec2> vertices;
  };

  struct DebugResult {
    // True if the input is free from cycles.
    bool cycle_free = false;
    // If it has a cycle, an example cycle given as face indices.
    std::optional<std::vector<int>> cycle;
    // True if every face is involved and connected.
    bool is_connected = false;
    // True if the graph can be embedded in the plane without
    // overlap.
    bool is_planar = false;
    // If non-planar, an example of overlapping faces, given
    // as two face indices with f0 < f1.
    std::optional<std::pair<int, int>> overlapping_faces;

    // The 2D locations of each face. Note that these do
    // not appear in face order, but each has its distinct
    // face_idx.
    std::vector<PlacedFace> placed_faces;

    // True if the graph is acyclic (a tree), completely connected,
    // and planar.
    bool is_net = false;
    // An SVG displaying the unfolded polyhedron.
    SVG::Doc svg;
  };

  static DebugResult DebugUnfolding(const AugmentedPoly &aug,
                                    BitStringConstView unfolding);

  // Determines whether the unfolding is a valid net without producing
  // any debug information. This can assume that the unfolding is a
  // proper face-spanning tree (no cycles, no disconnected
  // components).
  static bool IsNet(const AugmentedPoly &aug,
                    BitStringConstView unfolding);

  static bool PolygonsOverlap(std::span<const vec2> a,
                              std::span<const vec2> b) {
    return !HasSeparatingAxis(a, b) && !HasSeparatingAxis(b, a);
  }

  // Checks if there's a separating axis parallel to edge normals of p1.
  // You need to check both (a, b) and (b, a) if you want to prove
  // two polyhedra are non-overlapping.
  static bool HasSeparatingAxis(std::span<const vec2> p1,
                                std::span<const vec2> p2) {
    for (size_t i = 0; i < p1.size(); ++i) {
      vec2 p = p1[i];
      vec2 q = p1[(i + 1) % p1.size()];
      vec2 edge = {q.x - p.x, q.y - p.y};
      double len = std::sqrt(edge.x * edge.x + edge.y * edge.y);
      if (len < 1e-9)
        continue;
      vec2 normal = {-edge.y / len, edge.x / len};

      double min1 = std::numeric_limits<double>::infinity();
      double max1 = -std::numeric_limits<double>::infinity();
      for (vec2 v : p1) {
        double d = v.x * normal.x + v.y * normal.y;
        if (d < min1) min1 = d;
        if (d > max1) max1 = d;
      }

      double min2 = std::numeric_limits<double>::infinity();
      double max2 = -std::numeric_limits<double>::infinity();
      for (vec2 v : p2) {
        double d = v.x * normal.x + v.y * normal.y;
        if (d < min2) min2 = d;
        if (d > max2) max2 = d;
      }

      if (max1 <= min2 + 1e-7 || max2 <= min1 + 1e-7) {
        return true;
      }
    }
    return false;
  }

};


#endif
