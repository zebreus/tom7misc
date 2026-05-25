
#include "examples.h"

#include <memory>
#include <optional>
#include <vector>

#include "albrecht.h"
#include "arcfour.h"
#include "base/print.h"
#include "bit-string.h"
#include "geom/polyhedra.h"
#include "periodically.h"
#include "randutil.h"
#include "solve-leaf.h"
#include "status-bar.h"
#include "union-find.h"

using Aug = Albrecht::AugmentedPoly;

static BitString Sample(ArcFour *rc, const Aug &aug,
                        std::optional<int> face_idx,
                        std::optional<int> edge_idx,
                        bool want_net, bool want_non_net) {
  const Faces &faces = *aug.poly.faces;
  int num_faces = faces.NumFaces();
  int num_edges = faces.NumEdges();

  if (edge_idx.has_value()) {
    CHECK(face_idx.has_value());
  }

  if (want_non_net && rc->Byte() > 200) {
    // If we still want non-nets, most of the time we'll try a mostly
    // depth-first approach. This tends to produce longer chains of
    // faces, which have a higher chance of self-intersection,
    // compared to the bushy graphs produced by Kruskal's algorithm
    // below.

    const int root = face_idx.value_or(RandTo(rc, num_faces));
    BitString unfolding(num_edges, false);
    BitString visited(num_faces, false);
    std::vector<int> stack = {root};
    visited.Set(root, true);

    while (!stack.empty()) {
      int cur = stack.back();

      std::vector<int> candidates;
      if (cur == root && edge_idx.has_value()) {
        // Forced edge. Only consider this one.
        candidates = {edge_idx.value()};
      } else {
        for (int e : aug.face_edges[cur]) {
          int next_face =
            (faces.edges[e].f0 == cur) ? faces.edges[e].f1 : faces.edges[e].f0;
          if (!visited[next_face]) {
            candidates.push_back(e);
          }
        }
      }

      if (candidates.empty()) {
        stack.pop_back();
      } else {
        int e = candidates[RandTo(rc, candidates.size())];
        int next_face =
          (faces.edges[e].f0 == cur) ? faces.edges[e].f1 : faces.edges[e].f0;
        visited.Set(next_face, true);
        unfolding.Set(e, true);

        if (face_idx.has_value() && cur == root) {
          // Prevent the root from gaining additional children by removing
          // it from the stack, forcing it to be a leaf.
          stack.pop_back();
        }

        stack.push_back(next_face);
      }
    }

    return unfolding;
  }

  // If we have a face_idx, use the leaf solver to sample.
  if (face_idx.has_value()) {
    return SolveLeaf::SampleFace(rc, aug, face_idx.value());
  }

  BitString unfolding(num_edges, false);
  std::vector<int> edges(num_edges);
  for (int i = 0; i < num_edges; ++i) edges[i] = i;
  Shuffle(rc, &edges);

  UnionFind uf(num_faces);
  for (int i : edges) {
    const Faces::Edge &edge = faces.edges[i];
    if (uf.Find(edge.f0) != uf.Find(edge.f1)) {
      uf.Union(edge.f0, edge.f1);
      unfolding.Set(i, true);
    }
  }

  return unfolding;
}

Examples GetSomeExamples(
    ArcFour *rc,
    const Aug &aug,
    std::optional<int> face_idx,
    std::optional<int> edge_idx,
    const std::optional<BitString> &example_net,
    int num_nets, int num_non_nets, bool verbose) {

  if (edge_idx.has_value()) {
    CHECK(face_idx.has_value());
  }

  std::unique_ptr<StatusBar> status;
  if (verbose) status = std::make_unique<StatusBar>(1);
  Periodically status_per(1.0);

  std::vector<BitString> seen_non_nets;

  auto AlreadySaw = [&](const BitString &unfolding) {
      for (const BitString &seen : seen_non_nets) {
        if (seen == unfolding) {
          return true;
        }
      }
      return false;
    };

  Examples examples;
  if (example_net.has_value()) {
    examples.nets.push_back(Albrecht::DebugUnfolding(aug, example_net.value()));
  }

  int64_t attempts = 0;
  while ((examples.non_nets.size() < num_non_nets ||
          examples.nets.empty()) &&
         attempts < 500000) {
    attempts++;
    BitString unfolding = Sample(rc, aug, face_idx, edge_idx,
                                 examples.nets.empty(),
                                 examples.non_nets.size() < num_non_nets);

    if (Albrecht::IsNet(aug, unfolding)) {
      if (examples.nets.empty()) {
        examples.nets.push_back(Albrecht::DebugUnfolding(aug, unfolding));
      }
    } else {
      if (examples.non_nets.size() < 3) {

        bool duplicate = AlreadySaw(unfolding);

        if (!duplicate) {
          seen_non_nets.push_back(unfolding);
          examples.non_nets.push_back(Albrecht::DebugUnfolding(aug, unfolding));
        }
      }
    }

    if (status != nullptr) {
      status_per.RunIf([&]{
          status->Status("{} attempts, {} net(s), {} non-net(s)\n",
                         attempts,
                         examples.nets.size(), examples.non_nets.size());
        });
    }
  }

  if (status != nullptr) {
    status->Remove();
    Print("In {} attempts, Got {} net(s) and {} non-net(s).\n",
          attempts, examples.nets.size(), examples.non_nets.size());
  }

  return examples;
}
