
#include "solve-leaf.h"

#include <algorithm>
#include <condition_variable>
#include <format>
#include <limits>
#include <memory>
#include <optional>
#include <thread>
#include <utility>
#include <vector>
#include <mutex>

#include "albrecht.h"
#include "arcfour.h"
#include "bit-string.h"
#include "geom/polyhedra.h"
#include "randutil.h"
#include "threadutil.h"
#include "union-find.h"
#include "yocto-math.h"

using AugmentedPoly = Albrecht::AugmentedPoly;

namespace {

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
  ArcFour rc;
};

// Collects the result of multiple processes searching in parallel.
struct ResultChannel {
  std::mutex m;
  std::condition_variable cv;
  bool should_die = false;
  std::optional<BitString> result;
  // Send the result (only the first time this is called), and notify
  // all threads that we're done.
  void Send(std::optional<BitString> r) {
    {
      MutexLock ml(&m);
      if (!should_die) {
        result = std::move(r);
      }
      should_die = true;
    }
    cv.notify_all();
  }
  // Check whether we succeeded in another thread.
  bool ShouldDie() {
    MutexLock ml(&m);
    return should_die;
  }
};

// One solver instance with shared state for the recursion.
struct RecSolver {
  // Arguments.
  std::shared_ptr<ResultChannel> result_channel;
  const AugmentedPoly &aug;
  const int input_face_idx;
  const int input_edge_idx;

  RecSolver(std::shared_ptr<ResultChannel> result_channel,
            const AugmentedPoly &aug, int face_idx, int edge_idx) :
    result_channel(std::move(result_channel)),
    aug(aug), input_face_idx(face_idx), input_edge_idx(edge_idx) {
  }

  // Checks if new_face overlaps with any face already in
  // state.placed_faces.
  bool CheckOverlap(const SearchState &state,
                    int src_idx,
                    const PlacedFace &new_face) {
    for (const PlacedFace &pf : state.placed_faces) {
      if (new_face.max_b.x <= pf.min_b.x + 1e-7 ||
          new_face.min_b.x >= pf.max_b.x - 1e-7 ||
          new_face.max_b.y <= pf.min_b.y + 1e-7 ||
          new_face.min_b.y >= pf.max_b.y - 1e-7) {
        continue;
      }

      if (pf.face_idx == src_idx) {
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

    return reached_count == num_faces;
  }

  // Recursive search.
  bool Search(SearchState &state, std::vector<int> &frontier) {
    // Success?
    if (state.placed_faces.size() == aug.poly.faces->NumFaces()) {
      return true;
    }

    // If no edges left but we haven't placed all faces, this branch
    // fails.
    if (frontier.empty()) return false;

    // Early pruning.
    if (!CanReachAllFaces(state)) {
      return false;
    }

    // Other thread won?
    if (result_channel->ShouldDie()) return false;

    // Pick a random edge.
    const int frontier_idx = RandTo(&state.rc, frontier.size());
    std::swap(frontier[frontier_idx], frontier.back());
    const int edge_idx = frontier.back();
    frontier.pop_back();
    CHECK(!state.forbidden_edges.Get(edge_idx)) << edge_idx;

    const Faces::Edge &edge = aug.poly.faces->edges[edge_idx];
    bool f0_visited = state.visited_faces.Get(edge.f0);
    bool f1_visited = state.visited_faces.Get(edge.f1);

    if (f0_visited && f1_visited) {
      // Including this edge would form a cycle in our face tree,
      // so we must not include it.
      state.forbidden_edges.Set(edge_idx, true);
      bool result = Search(state, frontier);
      state.forbidden_edges.Set(edge_idx, false);
      frontier.push_back(edge_idx);
      std::swap(frontier[frontier_idx], frontier.back());
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
      if (!CheckOverlap(state, v_idx, U)) {
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

    // Either way, return the edge to the frontier when we pop.
    frontier.push_back(edge_idx);
    std::swap(frontier[frontier_idx], frontier.back());
    return false;
  }

  void DoSearch() {
    int num_faces = aug.poly.faces->NumFaces();
    int num_edges = aug.poly.faces->NumEdges();

    SearchState state{
      .unfolding = BitString(num_edges, false),
      .visited_faces = BitString(num_faces, false),
      .forbidden_edges = BitString(num_edges, false),
      .rc = ArcFour("pseudorandom"),
    };

    // To force input_face_idx to be a leaf attached solely via input_edge_idx,
    // we explicitly forbid all of its other edges from being in the spanning tree.
    for (int e_idx : aug.face_edges[input_face_idx]) {
      if (e_idx != input_edge_idx) {
        state.forbidden_edges.Set(e_idx, true);
      }
    }

    PlacedFace initial_face;
    initial_face.face_idx = input_face_idx;

    // We do not require any convex hull conditions, so placing the initial
    // face at the origin with the identity transform works fine.
    initial_face.global_tf.x = {1.0, 0.0};
    initial_face.global_tf.y = {0.0, 1.0};
    initial_face.global_tf.o = {0.0, 0.0};

    initial_face.vertices.reserve(aug.polygons[input_face_idx].size());
    for (const vec2 &pt : aug.polygons[input_face_idx]) {
      initial_face.vertices.push_back(pt);
      initial_face.min_b.x = std::min(initial_face.min_b.x, pt.x);
      initial_face.min_b.y = std::min(initial_face.min_b.y, pt.y);
      initial_face.max_b.x = std::max(initial_face.max_b.x, pt.x);
      initial_face.max_b.y = std::max(initial_face.max_b.y, pt.y);
    }

    state.placed_faces.push_back(std::move(initial_face));
    state.visited_faces.Set(input_face_idx, true);

    std::vector<int> frontier;
    frontier.push_back(input_edge_idx);

    if (Search(state, frontier)) {
      result_channel->Send(state.unfolding);
    } else {
      result_channel->Send(std::nullopt);
    }
  }
};

struct ShotgunSolver {
  std::shared_ptr<ResultChannel> result_channel;
  const AugmentedPoly &aug;
  const int input_face_idx;
  const int input_edge_idx;
  ArcFour rc;

  ShotgunSolver(std::shared_ptr<ResultChannel> result_channel,
                const AugmentedPoly &aug, int face_idx, int edge_idx,
                uint64_t seed) :
    result_channel(std::move(result_channel)),
    aug(aug), input_face_idx(face_idx), input_edge_idx(edge_idx),
    rc(std::format("shot.{:x}", seed)) {
  }

  void Solve() {
    const Faces &faces = *aug.poly.faces;
    const int num_faces = faces.NumFaces();
    const int num_edges = faces.NumEdges();
    BitString unfolding(num_edges, false);
    UnionFind uf(num_faces);
    // The randomly-ordered set of edges that we'll try connecting
    // until we have a complete graph.
    std::vector<int> edges;
    for (int e = 0; e < num_edges; e++) {
      // We always include the target edge, and always cut the
      // other edges on the target face. So any edge on the
      // target face is excluded from this vector and handled
      // manually during initialization.
      const Faces::Edge &edge = faces.edges[e];
      if (edge.f0 != input_face_idx &&
          edge.f1 != input_face_idx) {
        edges.push_back(e);
      }
    }

    while (!result_channel->ShouldDie()) {
      // Reset state in place.
      uf.Reset();
      Shuffle(&rc, &edges);

      unfolding.Clear(false);
      // The target edge must be included.
      // The other edges on the face are not included, because
      // they are not in the edges vector.
      unfolding.Set(input_edge_idx, true);

      for (int i : edges) {
        const Faces::Edge &edge = faces.edges[i];
        if (uf.Find(edge.f0) != uf.Find(edge.f1)) {
          uf.Union(edge.f0, edge.f1);
          unfolding.Set(i, true);
        }
      }

      if (Albrecht::IsNet(aug, unfolding)) {
        result_channel->Send(unfolding);
        return;
      }
    }
  }

};

static std::optional<BitString>
MultiSolve(const AugmentedPoly &poly, int face_idx, int edge_idx) {
  std::shared_ptr<ResultChannel> result_channel =
    std::make_shared<ResultChannel>();

  std::vector<std::unique_ptr<std::thread>> threads;

  threads.emplace_back(std::make_unique<std::thread>([&]{
      RecSolver rec(result_channel, poly, face_idx, edge_idx);
      (void)rec.DoSearch();
    }));

  ArcFour rc("shot");
  static constexpr int NUM_SHOTGUN_THREADS = 6;
  for (int i = 0; i < NUM_SHOTGUN_THREADS; i++) {
    uint64_t seed = Rand64(&rc);
    threads.emplace_back(std::make_unique<std::thread>([&, seed]{
        ShotgunSolver ss(result_channel, poly, face_idx, edge_idx,
                         seed);
        ss.Solve();
      }));
  }

  // Wait for one to finish.

  std::unique_lock<std::mutex> lock(result_channel->m);
  result_channel->cv.wait(lock, [&]{ return result_channel->should_die; });
  std::optional<BitString> result = std::move(result_channel->result);
  lock.unlock();

  for (auto &t : threads) t->join();

  return result;
}

}  // namespace

std::optional<BitString> SolveLeaf::FindLeafUnfolding(
    const Albrecht::AugmentedPoly &aug,
    int face_idx,
    int edge_idx) {

  return MultiSolve(aug, face_idx, edge_idx);
}

