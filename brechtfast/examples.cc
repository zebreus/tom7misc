
#include "examples.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "albrecht.h"
#include "arcfour.h"
#include "base/logging.h"
#include "base/print.h"
#include "bit-string.h"
#include "geom/polyhedra.h"
#include "periodically.h"
#include "randutil.h"
#include "solve-dual-leaf.h"
#include "solve-leaf.h"
#include "solve-line.h"
#include "solve-strong.h"
#include "status-bar.h"
#include "union-find.h"
#include "util.h"

using Aug = Albrecht::AugmentedPoly;

Constraint ParseConstraints(std::vector<std::string> *args) {
  Constraint constraint = NoConstraint{};
  std::vector<std::string> new_args;

  for (size_t i = 0; i < args->size(); ) {
    std::string &arg = (*args)[i];
    if (arg == "-line") {
      CHECK(std::holds_alternative<NoConstraint>(constraint)) << "Just "
        "one constraint at a time!";
      constraint = LineConstraint{};
      i++;

    } else if (arg == "-hull") {
      CHECK(std::holds_alternative<NoConstraint>(constraint)) << "Just "
        "one constraint at a time!";
      CHECK(args->size() - i >= 3) << "-hull needs face_idx and edge_idx";
      std::optional<int64_t> f = Util::ParseInt64Opt((*args)[i + 1]);
      std::optional<int64_t> e = Util::ParseInt64Opt((*args)[i + 2]);
      CHECK(f.has_value() && e.has_value()) << "-hull args must be numbers";
      constraint = HullConstraint{(int)f.value(), (int)e.value()};
      i += 3;

    } else if (arg == "-leaf") {
      CHECK(std::holds_alternative<NoConstraint>(constraint)) << "Just "
        "one constraint at a time!";
      CHECK(args->size() - i >= 3) << "-leaf needs face_idx and edge_idx";
      std::optional<int64_t> f = Util::ParseInt64Opt((*args)[i + 1]);
      std::optional<int64_t> e = Util::ParseInt64Opt((*args)[i + 2]);
      CHECK(f.has_value() && e.has_value()) << "-leaf args must be numbers";
      constraint = LeafConstraint{(int)f.value(), (int)e.value()};
      i += 3;

    } else if (arg == "-leaf-face") {
      CHECK(std::holds_alternative<NoConstraint>(constraint)) << "Just "
        "one constraint at a time!";
      CHECK(args->size() - i >= 2) << "-leaf-face needs face_idx";
      std::optional<int64_t> f = Util::ParseInt64Opt((*args)[i + 1]);
      CHECK(f.has_value()) << "-leaf-face arg must be a number";
      constraint = LeafFaceConstraint{(int)f.value()};
      i += 2;

    } else if (arg == "-dual-leaf") {
      CHECK(std::holds_alternative<NoConstraint>(constraint)) << "Just "
        "one constraint at a time!";
      CHECK(args->size() - i >= 2) << "-dual-leaf needs edge_idx";
      std::optional<int64_t> e = Util::ParseInt64Opt((*args)[i + 1]);
      CHECK(e.has_value()) << "-dual-leaf arg must be a number";
      constraint = DualLeafConstraint{(int)e.value()};
      i += 2;

    } else if (arg == "-cut") {
      CHECK(std::holds_alternative<NoConstraint>(constraint)) << "Just "
        "one constraint at a time!";
      CHECK(args->size() - i >= 2) << "-cut needs comma-separted edges";
      std::vector<std::string> es = Util::Split((*args)[i + 1], ',');
      std::vector<int> cuts;
      int mx = 0;
      for (const std::string &e : es) {
        std::optional<int64_t> eo = Util::ParseInt64Opt(e);
        CHECK(eo.has_value()) << "-cut arg must be comma-separated numbers";
        mx = std::max((int)eo.value(), mx);
        cuts.push_back((int)eo.value());
      }
      BitString c(mx + 1, false);
      for (int e : cuts) c.Set(e, true);
      constraint = EdgesCutConstraint{.cut = std::move(c)};
      i += 2;

    } else if (arg == "-vertex") {
      CHECK(std::holds_alternative<NoConstraint>(constraint)) << "Just "
        "one constraint at a time!";
      CHECK(args->size() - i >= 2) << "-vertex needs vertex_idx";
      std::optional<int64_t> v = Util::ParseInt64Opt((*args)[i + 1]);
      CHECK(v.has_value()) << "-vertex arg must be a number";
      constraint = VertexConstraint{(int)v.value()};
      i += 2;

    } else {
      new_args.push_back(std::move(arg));
      i++;
    }
  }

  *args = std::move(new_args);
  return constraint;
}


static std::optional<BitString> Sample(ArcFour *rc, const Aug &aug,
                                       const Constraint &constraint,
                                       bool want_net, bool want_non_net) {
  const Faces &faces = *aug.poly.faces;
  int num_faces = faces.NumFaces();
  int num_edges = faces.NumEdges();

  BitString forced_cut_edges(num_edges, false);

  if (const HullConstraint *c = std::get_if<HullConstraint>(&constraint)) {
    // TODO: Use a "sampler" strategy here.
    return SolveStrong::FindStrongUnfolding(aug, c->face_idx, c->edge_idx);
  } else if (std::holds_alternative<LineConstraint>(constraint)) {
    return SolveLine::SampleLine(rc, aug);
  } else if (const DualLeafConstraint *c =
          std::get_if<DualLeafConstraint>(&constraint)) {
    return SolveDualLeaf::SampleDualLeaf(rc, aug, c->edge_idx);
  }

  std::optional<int> face_idx;
  std::optional<int> edge_idx;
  if (auto *c = std::get_if<LeafFaceConstraint>(&constraint)) {
    face_idx = c->face_idx;
  } else if (auto *c = std::get_if<LeafConstraint>(&constraint)) {
    face_idx = c->face_idx;
    edge_idx = c->edge_idx;
  } else if (auto *c = std::get_if<EdgesCutConstraint>(&constraint)) {
    CHECK(c->cut.Size() <= num_edges) << "edges cut constraint has edges "
      "that are out of bounds!";
    for (int i = 0; i < c->cut.Size(); i++) {
      forced_cut_edges.Set(i, c->cut[i]);
    }
  } else if (auto *c = std::get_if<VertexConstraint>(&constraint)) {
    for (int i = 0; i < num_edges; i++) {
      if (faces.edges[i].v0 == c->vertex_idx ||
          faces.edges[i].v1 == c->vertex_idx) {
        forced_cut_edges.Set(i, true);
      }
    }
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
          if (forced_cut_edges[e]) continue;
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
    if (forced_cut_edges[i]) continue;
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
    const Constraint constraint,
    const std::optional<BitString> &example_net,
    int num_nets, int num_non_nets, bool verbose) {

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
  auto KeepGoing = [&]() {
      // All done.
      if (examples.non_nets.size() >= num_non_nets &&
          examples.nets.size() >= num_nets)
        return false;

      // Many polyhedra have ONLY nets.
      if (!examples.nets.empty() &&
          attempts > 100000)
        return false;

      return attempts < 500000;
    };

  while (KeepGoing()) {
    attempts++;
    std::optional<BitString> opt_unfolding =
        Sample(rc, aug, constraint,
               examples.nets.empty(),
               examples.non_nets.size() < num_non_nets);

    if (!opt_unfolding.has_value()) {
      break;
    }
    BitString unfolding = std::move(opt_unfolding.value());

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

    if (std::holds_alternative<HullConstraint>(constraint)) {
      // FindStrongUnfolding is deterministic, so don't loop on it.
      break;
    }
  }

  if (status != nullptr) {
    status->Remove();
    Print("In {} attempts, Got {} net(s) and {} non-net(s).\n",
          attempts, examples.nets.size(), examples.non_nets.size());
  }

  return examples;
}
