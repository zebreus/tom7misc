
#include "solve-vertex.h"

#include <algorithm>
#include <condition_variable>
#include <format>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

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

struct PlacedFace {
  int face_idx = 0;
  std::vector<vec2> vertices;
  vec2 min_b = {std::numeric_limits<double>::infinity(),
                std::numeric_limits<double>::infinity()};
  vec2 max_b = {-std::numeric_limits<double>::infinity(),
                -std::numeric_limits<double>::infinity()};
  frame2 global_tf;
};

struct SearchState {
  BitString unfolding;
  BitString visited_faces;
  BitString forbidden_edges;
  std::vector<PlacedFace> placed_faces;
  ArcFour rc;
};

struct ResultChannel {
  std::mutex m;
  std::condition_variable cv;
  bool should_die = false;
  std::optional<BitString> result;

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

  bool ShouldDie() {
    MutexLock ml(&m);
    return should_die;
  }
};

struct RecSolver {
  std::shared_ptr<ResultChannel> result_channel;
  const AugmentedPoly &aug;
  const int vertex_idx;

  RecSolver(std::shared_ptr<ResultChannel> result_channel,
            const AugmentedPoly &aug, int vertex_idx) :
    result_channel(std::move(result_channel)),
    aug(aug), vertex_idx(vertex_idx) {}

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

  bool CanReachAllFaces(const SearchState &state) {
    int num_faces = aug.poly.faces->NumFaces();
    int reached_count = state.placed_faces.size();
    if (reached_count == num_faces) return true;

    BitString reached = state.visited_faces;
    std::vector<int> stack;
    stack.reserve(num_faces);

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

  bool Search(SearchState &state, std::vector<int> &frontier) {
    if (state.placed_faces.size() == aug.poly.faces->NumFaces()) {
      return true;
    }

    if (frontier.empty()) return false;

    if (!CanReachAllFaces(state)) {
      return false;
    }

    if (result_channel->ShouldDie()) return false;

    const int frontier_idx = RandTo(&state.rc, frontier.size());
    std::swap(frontier[frontier_idx], frontier.back());
    const int edge_idx = frontier.back();
    frontier.pop_back();
    CHECK(!state.forbidden_edges.Get(edge_idx)) << edge_idx;

    const Faces::Edge &edge = aug.poly.faces->edges[edge_idx];
    bool f0_visited = state.visited_faces.Get(edge.f0);
    bool f1_visited = state.visited_faces.Get(edge.f1);

    if (f0_visited && f1_visited) {
      state.forbidden_edges.Set(edge_idx, true);
      bool result = Search(state, frontier);
      state.forbidden_edges.Set(edge_idx, false);
      frontier.push_back(edge_idx);
      std::swap(frontier[frontier_idx], frontier.back());
      return result;
    }

    const int v_idx = f0_visited ? edge.f0 : edge.f1;
    const int u_idx = f0_visited ? edge.f1 : edge.f0;

    {
      frame2 v_global_tf;
      for (const PlacedFace &pf : state.placed_faces) {
        if (pf.face_idx == v_idx) {
          v_global_tf = pf.global_tf;
          break;
        }
      }

      const auto &[f10, f01] = aug.edge_transforms[edge_idx];
      frame2 edge_tf = (edge.f0 == v_idx) ? f10 : f01;

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

      if (!CheckOverlap(state, v_idx, U)) {
        state.placed_faces.push_back(U);
        state.visited_faces.Set(u_idx, 1);
        state.unfolding.Set(edge_idx, 1);

        size_t old_frontier_size = frontier.size();

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

        frontier.resize(old_frontier_size);
        state.unfolding.Set(edge_idx, false);
        state.visited_faces.Set(u_idx, false);
        state.placed_faces.pop_back();
      }
    }

    {
      state.forbidden_edges.Set(edge_idx, true);
      if (Search(state, frontier)) {
        return true;
      }
      state.forbidden_edges.Set(edge_idx, false);
    }

    frontier.push_back(edge_idx);
    std::swap(frontier[frontier_idx], frontier.back());
    return false;
  }

  void DoSearch() {
    int num_faces = aug.poly.faces->NumFaces();
    int num_edges = aug.poly.faces->NumEdges();
    const Faces &faces = *aug.poly.faces;

    if (result_channel->ShouldDie()) return;

    SearchState state{
      .unfolding = BitString(num_edges, false),
      .visited_faces = BitString(num_faces, false),
      .forbidden_edges = BitString(num_edges, false),
      .placed_faces = {},
      .rc = ArcFour("pseudorandom"),
    };

    for (int e = 0; e < num_edges; ++e) {
      const auto &edge = faces.edges[e];
      if (edge.v0 == vertex_idx || edge.v1 == vertex_idx) {
        state.forbidden_edges.Set(e, true);
      }
    }

    PlacedFace initial_face;
    initial_face.face_idx = 0;

    initial_face.global_tf.x = {1.0, 0.0};
    initial_face.global_tf.y = {0.0, 1.0};
    initial_face.global_tf.o = {0.0, 0.0};

    initial_face.vertices.reserve(aug.polygons[0].size());
    for (const vec2 &pt : aug.polygons[0]) {
      initial_face.vertices.push_back(pt);
      initial_face.min_b.x = std::min(initial_face.min_b.x, pt.x);
      initial_face.min_b.y = std::min(initial_face.min_b.y, pt.y);
      initial_face.max_b.x = std::max(initial_face.max_b.x, pt.x);
      initial_face.max_b.y = std::max(initial_face.max_b.y, pt.y);
    }

    state.placed_faces.push_back(std::move(initial_face));
    state.visited_faces.Set(0, true);

    std::vector<int> frontier;
    for (int e : aug.face_edges[0]) {
      if (!state.forbidden_edges.Get(e)) {
        frontier.push_back(e);
      }
    }

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
  const int vertex_idx;
  ArcFour rc;

  ShotgunSolver(std::shared_ptr<ResultChannel> result_channel,
                const AugmentedPoly &aug, int vertex_idx,
                uint64_t seed) :
    result_channel(std::move(result_channel)),
    aug(aug), vertex_idx(vertex_idx),
    rc(std::format("shot.{:x}", seed)) {}

  void Solve() {
    const Faces &faces = *aug.poly.faces;
    const int num_faces = faces.NumFaces();
    const int num_edges = faces.NumEdges();
    BitString unfolding(num_edges, false);
    UnionFind uf(num_faces);

    std::vector<int> edges;
    for (int e = 0; e < num_edges; e++) {
      const Faces::Edge &edge = faces.edges[e];
      if (edge.v0 == vertex_idx || edge.v1 == vertex_idx) continue;
      edges.push_back(e);
    }

    while (!result_channel->ShouldDie()) {
      uf.Reset();
      Shuffle(&rc, &edges);

      unfolding.Clear(false);

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
MultiSolve(const AugmentedPoly &poly, int vertex_idx) {
  std::shared_ptr<ResultChannel> result_channel =
    std::make_shared<ResultChannel>();

  std::vector<std::unique_ptr<std::thread>> threads;

  threads.emplace_back(std::make_unique<std::thread>([&]{
      RecSolver rec(result_channel, poly, vertex_idx);
      rec.DoSearch();
    }));

  ArcFour rc("shot");
  static constexpr int NUM_SHOTGUN_THREADS = 6;
  for (int i = 0; i < NUM_SHOTGUN_THREADS; i++) {
    uint64_t seed = Rand64(&rc);
    threads.emplace_back(std::make_unique<std::thread>([&, seed]{
        ShotgunSolver ss(result_channel, poly, vertex_idx, seed);
        ss.Solve();
      }));
  }

  std::unique_lock<std::mutex> lock(result_channel->m);
  result_channel->cv.wait(lock, [&]{ return result_channel->should_die; });
  std::optional<BitString> result = std::move(result_channel->result);
  lock.unlock();

  for (auto &t : threads) t->join();

  return result;
}

}  // namespace

std::optional<BitString> SolveVertex::FindVertexUnfolding(
    const Albrecht::AugmentedPoly &aug,
    int vertex_idx) {
  CHECK(vertex_idx >= 0 && vertex_idx < aug.poly.vertices.size());
  return MultiSolve(aug, vertex_idx);
}

BitString SolveVertex::SampleVertex(
    ArcFour *rc,
    const Albrecht::AugmentedPoly &aug,
    int vertex_idx) {
  const Faces &faces = *aug.poly.faces;
  const int num_faces = faces.NumFaces();
  const int num_edges = faces.NumEdges();

  std::vector<int> edges;
  edges.reserve(num_edges);
  for (int e = 0; e < num_edges; e++) {
    const Faces::Edge &edge = faces.edges[e];
    if (edge.v0 == vertex_idx || edge.v1 == vertex_idx) continue;
    edges.push_back(e);
  }

  BitString unfolding(num_edges, false);
  UnionFind uf(num_faces);

  Shuffle(rc, &edges);
  for (int i : edges) {
    const Faces::Edge &edge = faces.edges[i];
    if (uf.Find(edge.f0) != uf.Find(edge.f1)) {
      uf.Union(edge.f0, edge.f1);
      unfolding.Set(i, true);
    }
  }

  return unfolding;
}


