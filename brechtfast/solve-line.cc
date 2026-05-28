
#include "solve-line.h"

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

  RecSolver(std::shared_ptr<ResultChannel> result_channel,
            const AugmentedPoly &aug) :
    result_channel(std::move(result_channel)),
    aug(aug) {
  }

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

    int start_f = -1;
    for (int f = 0; f < num_faces; f++) {
      if (!reached.Get(f)) {
        start_f = f;
        break;
      }
    }
    if (start_f == -1) return true;

    stack.push_back(start_f);
    reached.Set(start_f, 1);
    reached_count++;

    while (!stack.empty()) {
      int f = stack.back();
      stack.pop_back();

      for (int edge_idx : aug.face_edges[f]) {
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

  bool Search(SearchState &state, int current_face) {
    if (state.placed_faces.size() == aug.poly.faces->NumFaces()) {
      return true;
    }

    if (!CanReachAllFaces(state)) {
      return false;
    }

    if (result_channel->ShouldDie()) return false;

    std::vector<int> candidate_edges;
    for (int edge_idx : aug.face_edges[current_face]) {
      const Faces::Edge &edge = aug.poly.faces->edges[edge_idx];
      int next_f = (edge.f0 == current_face) ? edge.f1 : edge.f0;
      if (!state.visited_faces.Get(next_f)) {
        candidate_edges.push_back(edge_idx);
      }
    }

    if (candidate_edges.empty()) return false;

    Shuffle(&state.rc, &candidate_edges);

    frame2 v_global_tf;
    for (const PlacedFace &pf : state.placed_faces) {
      if (pf.face_idx == current_face) {
        v_global_tf = pf.global_tf;
        break;
      }
    }

    for (int edge_idx : candidate_edges) {
      const Faces::Edge &edge = aug.poly.faces->edges[edge_idx];
      const int u_idx = (edge.f0 == current_face) ? edge.f1 : edge.f0;

      const auto &[f10, f01] = aug.edge_transforms[edge_idx];
      frame2 edge_tf = (edge.f0 == current_face) ? f10 : f01;

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

      if (!CheckOverlap(state, current_face, U)) {
        state.placed_faces.push_back(U);
        state.visited_faces.Set(u_idx, 1);
        state.unfolding.Set(edge_idx, 1);

        if (Search(state, u_idx)) {
          return true;
        }

        state.unfolding.Set(edge_idx, false);
        state.visited_faces.Set(u_idx, false);
        state.placed_faces.pop_back();
      }
    }

    return false;
  }

  void DoSearch() {
    int num_faces = aug.poly.faces->NumFaces();
    int num_edges = aug.poly.faces->NumEdges();

    ArcFour start_rc("pseudorandom_start");
    std::vector<int> start_faces;
    start_faces.reserve(num_faces);
    for (int i = 0; i < num_faces; i++) {
      start_faces.push_back(i);
    }
    Shuffle(&start_rc, &start_faces);

    for (int start_face_idx : start_faces) {
      if (result_channel->ShouldDie()) return;

      SearchState state{
        .unfolding = BitString(num_edges, false),
        .visited_faces = BitString(num_faces, false),
        .placed_faces = {},
        .rc = ArcFour(std::format("pseudorandom_{}", start_face_idx)),
      };

      PlacedFace initial_face;
      initial_face.face_idx = start_face_idx;

      initial_face.global_tf.x = {1.0, 0.0};
      initial_face.global_tf.y = {0.0, 1.0};
      initial_face.global_tf.o = {0.0, 0.0};

      initial_face.vertices.reserve(aug.polygons[start_face_idx].size());
      for (const vec2 &pt : aug.polygons[start_face_idx]) {
        initial_face.vertices.push_back(pt);
        initial_face.min_b.x = std::min(initial_face.min_b.x, pt.x);
        initial_face.min_b.y = std::min(initial_face.min_b.y, pt.y);
        initial_face.max_b.x = std::max(initial_face.max_b.x, pt.x);
        initial_face.max_b.y = std::max(initial_face.max_b.y, pt.y);
      }

      state.placed_faces.push_back(std::move(initial_face));
      state.visited_faces.Set(start_face_idx, true);

      if (Search(state, start_face_idx)) {
        result_channel->Send(state.unfolding);
        return;
      }
    }

    result_channel->Send(std::nullopt);
  }
};

struct ShotgunSolver {
  std::shared_ptr<ResultChannel> result_channel;
  const AugmentedPoly &aug;
  ArcFour rc;

  ShotgunSolver(std::shared_ptr<ResultChannel> result_channel,
                const AugmentedPoly &aug,
                uint64_t seed) :
    result_channel(std::move(result_channel)),
    aug(aug),
    rc(std::format("shot.{:x}", seed)) {
  }

  void Solve() {
    while (!result_channel->ShouldDie()) {
      if (std::optional<BitString> ounf =
          SolveLine::SampleLine(&rc, aug)) {
        if (Albrecht::IsNet(aug, ounf.value())) {
          result_channel->Send(ounf.value());
          return;
        }
      }
    }
  }
};

static std::optional<BitString> MultiSolve(const AugmentedPoly &poly) {
  std::shared_ptr<ResultChannel> result_channel =
    std::make_shared<ResultChannel>();

  std::vector<std::unique_ptr<std::thread>> threads;

  threads.emplace_back(std::make_unique<std::thread>([&]{
      RecSolver rec(result_channel, poly);
      rec.DoSearch();
    }));

  ArcFour rc("shot");
  static constexpr int NUM_SHOTGUN_THREADS = 6;
  for (int i = 0; i < NUM_SHOTGUN_THREADS; i++) {
    uint64_t seed = Rand64(&rc);
    threads.emplace_back(std::make_unique<std::thread>([&, seed]{
        ShotgunSolver ss(result_channel, poly, seed);
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

std::optional<BitString> SolveLine::FindLineUnfolding(
    const Albrecht::AugmentedPoly &aug) {
  return MultiSolve(aug);
}

std::optional<BitString> SolveLine::SampleLine(
    ArcFour *rc, const Albrecht::AugmentedPoly &aug,
    int max_attempts) {
  const Faces &faces = *aug.poly.faces;
  const int num_faces = faces.NumFaces();
  const int num_edges = faces.NumEdges();

  for (int tries = 0; tries < max_attempts; tries++) {
    BitString unfolding(num_edges, false);
    BitString visited(num_faces, false);

    int current_face = RandTo(rc, num_faces);
    visited.Set(current_face, true);

    int length = 1;
    bool stuck = false;

    while (length < num_faces) {
      std::vector<int> candidates;
      for (int edge_idx : aug.face_edges[current_face]) {
        const Faces::Edge &edge = faces.edges[edge_idx];
        int next_f = (edge.f0 == current_face) ? edge.f1 : edge.f0;
        if (!visited.Get(next_f)) {
          candidates.push_back(edge_idx);
        }
      }

      if (candidates.empty()) {
        stuck = true;
        break;
      }

      int edge_idx = candidates[RandTo(rc, candidates.size())];
      const Faces::Edge &edge = faces.edges[edge_idx];
      current_face = (edge.f0 == current_face) ? edge.f1 : edge.f0;

      visited.Set(current_face, true);
      unfolding.Set(edge_idx, true);
      length++;
    }

    if (!stuck) {
      return {std::move(unfolding)};
    }
  }

  return std::nullopt;
}

