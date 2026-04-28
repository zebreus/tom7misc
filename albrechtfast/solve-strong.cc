
#include "solve-strong.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

#include "albrecht.h"
#include "bit-string.h"
#include "geom/polyhedra.h"
#include "yocto-math.h"

using AugmentedPoly = Albrecht::AugmentedPoly;

namespace {

// There are exponentially many of these, but it's often easy to find
// unfoldings (or easy to rule out entire subtrees) so exploring them
// exhaustively works fine for small polyhedra.

// Represents a face that has been successfully laid out in 2D.
struct PlacedFace {
  int face_idx = 0;
  std::vector<vec2> vertices;
  // Bounding box for fast AABB overlap rejection.
  vec2 min_b = {std::numeric_limits<double>::infinity(),
                std::numeric_limits<double>::infinity()};
  vec2 max_b = {-std::numeric_limits<double>::infinity(),
                -std::numeric_limits<double>::infinity()};
  // The transform used to place this face.
  frame2 global_tf;
};

// Mutable state used during the backtracking search.
struct SearchState {
  // The edges chosen so far to form the net.
  // Representation invariant: This represents a tree rooted
  // at the starting face.
  // num_edges in size.
  BitString unfolding;
  // Faces currently in the spanning tree.
  // num_faces in size.
  BitString visited_faces;
  // Edges we may not consider.
  // num_edges in size.
  BitString forbidden_edges;
  std::vector<PlacedFace> placed_faces;
};

// One solver instance with shared state for the recursion.
struct Solver {
  // Arguments.
  const AugmentedPoly &aug;
  const int input_face_idx;
  const int input_edge_idx;

  Solver(const AugmentedPoly &aug, int face_idx, int edge_idx) :
    aug(aug), input_face_idx(face_idx), input_edge_idx(edge_idx) {

  }

  // Check whether new_face can be placed, or whether it would violate
  // our requirement that the input edge be on the convex hull.
  static bool CheckBounds(const PlacedFace &new_face) {
    // We allow a little slop here, since numerical precision could
    // cause a series of rotations to make a point look like it is
    // outside when it's really not (or colinear).
    return new_face.min_b.x < -1.0e-5;
  }

  // Checks if new_face overlaps with any face already in
  // state.placed_faces.
  bool CheckOverlap(const SearchState &state, const PlacedFace &new_face) {
    for (const PlacedFace &pf : state.placed_faces) {
      if (new_face.max_b.x <= pf.min_b.x + 1e-7 ||
          new_face.min_b.x >= pf.max_b.x - 1e-7 ||
          new_face.max_b.y <= pf.min_b.y + 1e-7 ||
          new_face.min_b.y >= pf.max_b.y - 1e-7) {
        continue;
      }

      if (Albrecht::PolygonsOverlap(new_face.vertices, pf.vertices)) {
        return true;
      }
    }
    return false;
  }

  // Pruning via reachability.
  // Since we branch by either including or excluding an edge, we might
  // exclude enough edges to disconnect the unvisited faces from the tree.
  // A quick BFS/DFS here (ignoring forbidden_edges) can prune dead ends early.
  bool CanReachAllFaces(const SearchState &state) {
    int num_faces = aug.poly.faces->NumFaces();
    int reached_count = state.placed_faces.size();
    if (reached_count == num_faces) return true;

    BitString reached = state.visited_faces;
    std::vector<int> stack;
    stack.reserve(num_faces);

    // Seed the search with all faces currently in our spanning tree.
    for (const PlacedFace &pf : state.placed_faces) {
      stack.push_back(pf.face_idx);
    }

    while (!stack.empty()) {
      int f = stack.back();
      stack.pop_back();

      for (int edge_idx : aug.face_edges[f]) {
        if (state.forbidden_edges.Get(edge_idx)) continue;

        const Faces::Edge &edge = aug.poly.faces->edges[edge_idx];
        int next_f = (edge.f0 == f) ? edge.f1 : edge.f0;

        if (!reached.Get(next_f)) {
          reached.Set(next_f, 1);
          reached_count++;
          if (reached_count == num_faces) return true;
          stack.push_back(next_f);
        }
      }
    }

    if (reached_count < num_faces) return false;
    return true;
  }

  // Recursive search.
  //
  // The frontier is the queue of candidate edges. We never put a
  // forbidden edge into the queue. To process each edge, we try
  // both including it and not including it recursively.
  bool Search(SearchState &state, std::vector<int> &frontier) {
    // Success?
    if (state.placed_faces.size() == aug.poly.faces->NumFaces()) {
      return true;
    }

    // Early pruning.
    if (!CanReachAllFaces(state)) {
      return false;
    }

    // If no edges left but we haven't placed all faces, this branch
    // fails.
    if (frontier.empty()) return false;

    const int edge_idx = frontier.back();
    frontier.pop_back();
    CHECK(!state.forbidden_edges.Get(edge_idx));

    const Faces::Edge &edge = aug.poly.faces->edges[edge_idx];
    bool f0_visited = state.visited_faces.Get(edge.f0);
    bool f1_visited = state.visited_faces.Get(edge.f1);

    if (f0_visited && f1_visited) {
      // Including this edge would form a cycle in our face tree,
      // so we must not include it. Note we probably don't need
      // to forbid it for the recursive search, but it's easier
      // to understand if we do.
      state.forbidden_edges.Set(edge_idx, true);
      bool result = Search(state, frontier);
      state.forbidden_edges.Set(edge_idx, false);
      frontier.push_back(edge_idx);
      return result;
    }

    // Let V be the visited face, and U be the unvisited face.
    const int v_idx = f0_visited ? edge.f0 : edge.f1;
    const int u_idx = f0_visited ? edge.f1 : edge.f0;

    {
      // Try taking the edge.

      // Calculate U's frame2 using V's global_tf and the aug.edge_transforms.
      frame2 v_global_tf;
      for (const PlacedFace &pf : state.placed_faces) {
        if (pf.face_idx == v_idx) {
          v_global_tf = pf.global_tf;
          break;
        }
      }

      const auto &[f10, f01] = aug.edge_transforms[edge_idx];
      frame2 edge_tf = (edge.f0 == v_idx) ? f10 : f01;

      // Compute what U would look like if placed.
      PlacedFace U;
      U.face_idx = u_idx;
      U.global_tf = v_global_tf * edge_tf;
      U.vertices.reserve(aug.polygons[u_idx].size());

      for (const vec2 &v : aug.polygons[u_idx]) {
        vec2 tv = yocto::transform_point(U.global_tf, v);
        U.vertices.push_back(tv);
        U.min_b.x = std::min(U.min_b.x, tv.x);
        U.min_b.y = std::min(U.min_b.y, tv.y);
        U.max_b.x = std::max(U.max_b.x, tv.x);
        U.max_b.y = std::max(U.max_b.y, tv.y);
      }

      // See if it fits.
      if (!CheckBounds(U) && !CheckOverlap(state, U)) {
        state.placed_faces.push_back(U);
        state.visited_faces.Set(u_idx, 1);
        state.unfolding.Set(edge_idx, 1);

        size_t old_frontier_size = frontier.size();

        // Push U's incident edges (that aren't forbidden/visited) to frontier.
        for (int u_edge_idx : aug.face_edges[u_idx]) {
          if (u_edge_idx == edge_idx) continue;

          const Faces::Edge &ue = aug.poly.faces->edges[u_edge_idx];
          int other_f = (ue.f0 == u_idx) ? ue.f1 : ue.f0;

          if (!state.visited_faces.Get(other_f) &&
              !state.forbidden_edges.Get(u_edge_idx)) {
            frontier.push_back(u_edge_idx);
          }
        }

        if (Search(state, frontier)) {
          return true;
        }

        // Undo the state changes.
        frontier.resize(old_frontier_size);
        state.unfolding.Set(edge_idx, false);
        state.visited_faces.Set(u_idx, false);
        state.placed_faces.pop_back();
      }
    }

    {
      // Try not taking the edge.

      state.forbidden_edges.Set(edge_idx, true);
      if (Search(state, frontier)) {
        return true;
      }

      state.forbidden_edges.Set(edge_idx, false);
    }

    // Either way, return the edge to the frontier when we
    // pop.
    frontier.push_back(edge_idx);
    return false;
  }

  std::optional<BitString> DoSearch() {
    // Place the first face rotated so that the input edge is vertical
    // at x=0. This gives us a fast test for when we place a face:
    // None of its vertices can have x < 0 (or else the input edge
    // would not be on the 2D convex hull, as required).

    int num_faces = aug.poly.faces->NumFaces();
    int num_edges = aug.poly.faces->NumEdges();

    SearchState state;
    state.unfolding = BitString(num_edges, false);
    state.visited_faces = BitString(num_faces, false);
    state.forbidden_edges = BitString(num_edges, false);

    // Can't cut the input edge.
    state.forbidden_edges.Set(input_edge_idx, true);

    int edge_index_in_face = 0;
    const auto &face_edges = aug.face_edges[input_face_idx];
    for (size_t i = 0; i < face_edges.size(); ++i) {
      if (face_edges[i] == input_edge_idx) {
        edge_index_in_face = i;
        break;
      }
    }

    const std::vector<vec2> &poly = aug.polygons[input_face_idx];
    vec2 p0 = poly[edge_index_in_face];
    vec2 p1 = poly[(edge_index_in_face + 1) % poly.size()];
    vec2 v = yocto::normalize(p1 - p0);

    PlacedFace initial_face;
    initial_face.face_idx = input_face_idx;

    // Construct a frame2 that maps p0 to (0,0) and the direction v to (0,1).
    initial_face.global_tf.x = {v.y, v.x};
    initial_face.global_tf.y = {-v.x, v.y};
    initial_face.global_tf.o = -(initial_face.global_tf.x * p0.x +
                                 initial_face.global_tf.y * p0.y);

    initial_face.vertices.reserve(poly.size());
    for (const vec2 &pt : poly) {
      vec2 tv = yocto::transform_point(initial_face.global_tf, pt);
      initial_face.vertices.push_back(tv);
      initial_face.min_b.x = std::min(initial_face.min_b.x, tv.x);
      initial_face.min_b.y = std::min(initial_face.min_b.y, tv.y);
      initial_face.max_b.x = std::max(initial_face.max_b.x, tv.x);
      initial_face.max_b.y = std::max(initial_face.max_b.y, tv.y);
    }

    state.placed_faces.push_back(std::move(initial_face));
    state.visited_faces.Set(input_face_idx, true);

    std::vector<int> frontier;
    for (int e_idx : aug.face_edges[input_face_idx]) {
      if (e_idx != input_edge_idx) {
        frontier.push_back(e_idx);
      }
    }

    if (Search(state, frontier)) {
      return state.unfolding;
    }

    return std::nullopt;
  }

};

}  // namespace


std::optional<BitString> SolveStrong::FindStrongUnfolding(
    const Albrecht::AugmentedPoly &aug,
    int face_idx,
    int edge_idx) {

  Solver solver(aug, face_idx, edge_idx);

  return solver.DoSearch();
}
