
#include "albrecht.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <format>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <cstdint>

#include "ansi.h"
#include "arcfour.h"
#include "atomic-util.h"
#include "auto-histo.h"
#include "bit-string.h"
#include "db.h"
#include "geom/polyhedra.h"
#include "periodically.h"
#include "randutil.h"
#include "sampler.h"
#include "solve-leaf.h"
#include "status-bar.h"
#include "threadutil.h"
#include "timer.h"
#include "util.h"

using Aug = Albrecht::AugmentedPoly;
using OneSample = Sampler::OneSample;


DECLARE_COUNTERS(ctr_poly, ctr_samples, ctr_satisfied_leaf, ctr_saved);

static std::string FormatNum(uint64_t n) {
  if (n > 1'000'000) {
    double m = n / 1'000'000.0;
    if (m >= 1'000'000.0) {
      return std::format("{:.1f}T", m / 1'000'000.0);
    } else if (m >= 1000.0) {
      return std::format("{:.1f}B", m / 1000.0);
    } else if (m >= 100.0) {
      return std::format("{}M", (int)std::round(m));
    } else if (m > 10.0) {
      return std::format("{:.1f}M", m);
    } else {
      // TODO: Integer division. color decimal place and suffix.
      return std::format("{:.2f}M", m);
    }
  } else {
    return Util::UnsignedWithCommas(n);
  }
}


// (actually an upper bound, not inclusive)
static constexpr int MAX_FACES = 80;

struct Leaffast {

  static constexpr int METHOD =
    DB::METHOD_CONSTRUCT_LEAF;
  // DB::METHOD_RANDOM_SYMMETRIC;

  static constexpr int SAMPLES_PER_THREAD = 16384;
  static constexpr int NUM_THREADS = 8;

  ArcFour main_rc;

  static constexpr int FACE_HISTO_LINE = 1;
  static constexpr int EDGE_HISTO_LINE = 2;
  static constexpr int VERT_HISTO_LINE = 3;
  static constexpr int SAMPLE_LINE = 4;
  static constexpr int OVERALL_LINE = 5;
  StatusBar status = StatusBar(6);

  double time_sample = 0.0;
  double time_solve = 0.0;

  Leaffast() : main_rc(std::format("leaffast.{}", time(nullptr))) {

  }

  ~Leaffast() {

  }


  std::pair<Polyhedron, std::optional<std::pair<int, int>>>
  Sample(ArcFour *rc, int method) {
    switch (METHOD) {

    case DB::METHOD_RANDOM_CYCLIC: {
      const int num_verts = 8 + RandTo(rc, 54);
      return std::make_pair(
          Sampler::RandomCyclicPolyhedron(rc, num_verts),
          std::nullopt);
    }

    case DB::METHOD_RANDOM_SYMMETRIC: {
      const int num_verts = 8 + RandTo(rc, 54);
      return std::make_pair(
          Sampler::RandomSymmetricPolyhedron(rc, num_verts, MAX_FACES),
          std::nullopt);
    }

#if 0
    case DB::METHOD_OPT: {
      return {Sampler::OptSample(&status, rc), std::nullopt};
    }
#endif

    case DB::METHOD_CONSTRUCT:
      return std::make_pair(
          Sampler::MakeConstruct(&status, rc, MAX_FACES, false),
          std::nullopt);

    case DB::METHOD_CONSTRUCT_LEAF: {
      auto sample = Sampler::ConstructHardLeaf(&status, rc, MAX_FACES);
      return std::make_pair(std::move(sample.poly),
                            std::make_pair(sample.face_idx, sample.edge_idx));
    }

    default:
      LOG(FATAL) << "Bad method?";
    }
  }

  std::vector<int64_t> best_denom = std::vector<int64_t>(MAX_FACES, 1);

  void Run() {
    DB db;

    Periodically status_per(1.0);
    Periodically histo_per(10.0);
    Timer timer;

    const int64_t seed = Rand64(&main_rc);

    std::mutex m;
    AutoHisto face_histo, edge_histo, vert_histo;

    status.Print("Begin parallel...\n");
    fflush(stdout);
    ParallelFan(
        NUM_THREADS,
        [&](int thread_idx) {
          ArcFour rc(std::format("{}.{}", seed, thread_idx));
          status.Print("Started thread {}.\n", thread_idx);
          fflush(stdout);          for (;;) {

            Timer sample_timer;
            auto [poly_val, constraint] = Sample(&rc, METHOD);
            Aug aug(std::move(poly_val));
            ctr_poly++;
            const double sample_sec = sample_timer.Seconds();

            const Polyhedron &poly = aug.poly;
            const int num_faces = poly.faces->NumFaces();
            const int num_edges = poly.faces->NumEdges();
            const int num_verts = poly.faces->NumVertices();

            {
              MutexLock ml(&m);
              face_histo.Observe(num_faces);
              edge_histo.Observe(num_edges);
              vert_histo.Observe(num_verts);
            }

            const int64_t already_denom = [&]{
                MutexLock ml(&m);
                return best_denom[num_faces];
              }();

            // Try to find an edge/face pair that isn't solvable.
            Timer solve_timer;
            std::vector<std::pair<int, int>> candidates;
            if (constraint.has_value()) {
              candidates.push_back(constraint.value());
            } else {
              for (int e = 0; e < num_edges; e++) {
                const Faces::Edge &edge = poly.faces->edges[e];
                for (int f : {edge.f0, edge.f1}) {
                  candidates.push_back({f, e});
                }
              }
            }

            for (const auto &[f, e] : candidates) {

                // Require higher standard as we get a larger
                // number of faces. Also require beating our previous
                // record in this process so that we don't flood
                // the database.
                const int64_t MAX_SAMPLES =
                  std::max(already_denom + 1,
                           (int64_t)(131072 * sqrt(num_faces)));

                const bool is_hard = [&]{
                    for (int s = 0; s < MAX_SAMPLES; s++) {
                      BitString unfolding = SolveLeaf::SampleLeaf(
                          &rc, aug, f, e);

                      if (Albrecht::IsNet(aug, unfolding)) {
                        ctr_samples += s;
                        return false;
                      }
                    }

                    ctr_samples += MAX_SAMPLES;
                    return true;
                  }();

                const double solve_sec = solve_timer.Seconds();

                {
                  MutexLock ml(&m);
                  time_sample += sample_sec;
                  time_solve += solve_sec;
                }

                if (is_hard) {
                  // Write to database.
                  const DB::Why why = {DB::LeafIH{
                      .face_idx = f,
                      .edge_idx = e,
                    }};
                  // This approach inherently has netness 0, and
                  // no example.
                  db.AddHard(poly, why, METHOD, 0, MAX_SAMPLES,
                             std::nullopt);

                  ctr_saved++;
                  goto next;
                }
            }

            // Then we solved it for all!
            if (!constraint.has_value())
              ctr_satisfied_leaf++;

          next:;

            status_per.RunIf([&]{
                double wall_time = timer.Seconds();
                double pps = ctr_poly.Read() / wall_time;
                // Total over all threads. We assume all the time
                // goes into these two.
                double total_time = time_sample + time_solve;
                double sample_pct = (time_sample * 100.0) / total_time;
                double solve_pct = (time_solve * 100.0) / total_time;

                {
                MutexLock ml(&m);

                status.LineStatus(FACE_HISTO_LINE,
                                  "F {}\n",
                                  face_histo.OneLineANSI());
                status.LineStatus(EDGE_HISTO_LINE,
                                  "E {}\n",
                                  edge_histo.OneLineANSI());
                status.LineStatus(VERT_HISTO_LINE,
                                  "V {}\n",
                                  face_histo.OneLineANSI());
                }

                // Reserved for subprocess
                status.LineStatus(SAMPLE_LINE,
                                  "{}\n",
                                  Sampler::SampleStats());

                status.LineStatus(
                    OVERALL_LINE,
                    "{} polys ({:.2f}/s), {} samples, {}☘, {}💾. "
                    "{} ({:.1f}% + {:.1f}%) \n",
                    FormatNum(ctr_poly.Read()),
                    pps,
                    FormatNum(ctr_samples.Read()),
                    ctr_satisfied_leaf.Read(),
                    ctr_saved.Read(),
                    ANSI::Time(wall_time),
                    sample_pct, solve_pct);
              });
          }
        });
  }

};

int main(int argc, char **argv) {
  ANSI::Init();

  printf("Started...\n");
  fflush(stdout);

  {
    Leaffast leaffast;
    printf("Created...\n");
    fflush(stdout);
    leaffast.Run();
  }

  return 0;
}
