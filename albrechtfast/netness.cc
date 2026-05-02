
#include <algorithm>
#include <cmath>
#include <format>
#include <utility>
#include <cstdint>
#include <vector>

#include "albrecht.h"
#include "arcfour.h"
#include "bit-string.h"
#include "geom/polyhedra.h"
#include "netness.h"
#include "randutil.h"
#include "threadutil.h"
#include "union-find.h"

std::pair<int64_t, int64_t> Netness::Compute(uint64_t seed,
                                             const Aug &aug,
                                             int num_samples,
                                             int num_repeat,
                                             int num_threads) {
  const Faces &faces = *aug.poly.faces;
  const int num_faces = faces.NumFaces();
  const int num_edges = faces.NumEdges();

  // Just in case...
  num_threads = std::min(num_samples, num_threads);

  const int samples_per_thread =
    (int)std::ceil(num_samples / num_threads);

  // Run, but if we get zero numerator, run again several times.
  int64_t total_numer = 0, total_denom = 0;
  for (int repeats = 0; repeats < num_repeat; repeats++) {

    std::vector<int> nets(num_threads, 0);
    ParallelFan(
        num_threads,
        [&](int thread_idx) {
          ArcFour thread_rc(std::format("{}.{}", seed, thread_idx));

          int success = 0;

          std::vector<int> edges;
          for (int i = 0; i < num_edges; i++)
            edges.push_back(i);

          for (int sample = 0; sample < samples_per_thread; sample++) {
            BitString unfolding(num_edges, false);
            // Greedily connect them, but in some random order.
            Shuffle(&thread_rc, &edges);

            UnionFind uf(num_faces);
            for (int eidx : edges) {
              const Faces::Edge &edge = faces.edges[eidx];
              if (uf.Find(edge.f0) != uf.Find(edge.f1)) {
                uf.Union(edge.f0, edge.f1);
                unfolding.Set(eidx, true);
              }
            }

            if (Albrecht::IsNet(aug, unfolding)) {
              success++;
            }
          }

          nets[thread_idx] = success;
        });

    for (int s : nets) total_numer += s;
    total_denom += num_threads * samples_per_thread;
    if (total_numer > 0) break;
  }

  return std::make_pair(total_numer, total_denom);
}
